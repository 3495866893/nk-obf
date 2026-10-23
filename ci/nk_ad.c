// nk_ad.c —— Ensia 反调试强化模块（arm64 / Android，纯用户态）
//
// 设计目标：不依赖“字符串特征/进程名”，因此 Frida 去特征（改名 gadget、
// 隐藏模块名）也躲不掉：
//   1) prctl(PR_SET_DUMPABLE, 0)     阻止第三方 attach
//   2) 看门狗子进程 PTRACE_SEIZE 父进程，占住 trace 位；
//      父进程周期性核对 TracerPid == 看门狗 pid，若不是（被换/被摘）→ 退出
//   3) 周期性扫描 /proc/self/maps：出现“匿名可执行段”（无路径名的 r-x/rwx
//      且 > 64KB）→ 判定为注入代码（gadget/Stalker trampoline 的典型特征）
//   4) 关键函数序言哈希（懒记录基线）：被 inline hook 改动过 → 退出
//   5) 记录 SIGTRAP 残留（被下断点/单步）→ 退出
//
// 命中统一走 _exit(97)，不产生 core、不打印、不留栈。
//
// 编译：随目标一起编译（cat 进 SRCS），需要 libc + pthread（bionic 自带）。
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/ptrace.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <stdint.h>

static pid_t g_wd_pid = -1;      // 看门狗 pid（应为我们的唯一 tracer）
static volatile int g_stop = 0;
static uint64_t g_prologue = 0;  // 关键函数序言基线（懒记录）
static volatile int g_trap_seen = 0;

static void nk_die(void) {
    _exit(97);
}

// ---- 4) 序言哈希 ----------------------------------------------------------
__attribute__((noinline)) static void nk_decoy(void) {
    __asm__ __volatile__("" ::: "memory");
    volatile int x = 1;
    x += 2;
    (void)x;
}

static uint64_t nk_hash16(const void *p) {
    const uint64_t *w = (const uint64_t *)p;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < 2; i++) {
        h ^= w[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

// ---- 工具：读 /proc 文件 --------------------------------------------------
static int nk_read_file(const char *path, char *buf, int cap) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int n = (int)read(fd, buf, cap - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;
    return n;
}

static int nk_tracer_pid(void) {
    char buf[4096];
    if (nk_read_file("/proc/self/status", buf, sizeof buf) < 0) return -2;
    char *p = strstr(buf, "TracerPid:");
    if (!p) return -2;
    p += 10;
    while (*p == ' ' || *p == '\t') p++;
    return (int)strtol(p, NULL, 10);
}

// ---- 3) 匿名可执行段扫描 --------------------------------------------------
static int nk_anon_exec_segment(void) {
    char buf[65536];
    int n = nk_read_file("/proc/self/maps", buf, sizeof buf);
    if (n <= 0) return 0;
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        // 形如： 7a1b000000-7a1b001000 r-xp 00000000 00:00 0
        unsigned long lo = 0, hi = 0;
        char perms[8] = {0};
        if (sscanf(line, "%lx-%lx %7s", &lo, &hi, perms) == 3) {
            int exec = (perms[2] == 'x');
            int size = (int)(hi - lo);
            // 路径字段：跳过 4 个字段后的剩余部分
            char *sp = strchr(line, '/');
            int has_path = (sp != NULL);
            int is_special = (strstr(line, "[vdso]") || strstr(line, "[vvar]") ||
                              strstr(line, "[stack]") || strstr(line, "[vectors]") ||
                              strstr(line, "[sigpage]") || strstr(line, "[anon:.bss]"));
            if (exec && !has_path && !is_special && size > 64 * 1024)
                return 1;
        }
        line = nl ? nl + 1 : NULL;
    }
    return 0;
}

// ---- 5) SIGTRAP 残留 ------------------------------------------------------
static int nk_pending_trap(void) {
    char buf[4096];
    if (nk_read_file("/proc/self/status", buf, sizeof buf) < 0) return 0;
    // SigPnd/ShdPnd 是十六进制位图，第 5 位=SIGTRAP(5)
    const char *keys[2] = {"SigPnd:", "ShdPnd:"};
    for (int k = 0; k < 2; k++) {
        char *p = strstr(buf, keys[k]);
        if (!p) continue;
        p += strlen(keys[k]);
        unsigned long v = strtoul(p, NULL, 16);
        if (v & (1UL << (5 - 1))) return 1;
    }
    return 0;
}


// ---- 6) libc 序言 inline hook 特征扫描（arm64）---------------------------
// Frida 的 Interceptor 属于 inline hook：会把目标函数开头改成
//   b <trampoline>            (0x14xxxxxx)
//   ldr xN, #8 ; br xN        (0x58xxxxxx 之后 0xD61F0000)
//   br xN                     (0xD61F0000..)
// 这里直接看指令编码，不看模块名/字符串，因此“去特征/改名”无效。
#include <dlfcn.h>

static int nk_looks_hooked(const unsigned char *p) {
    unsigned w0 = 0, w1 = 0;
    memcpy(&w0, p, 4);
    memcpy(&w1, p + 4, 4);
    if ((w0 & 0xFC000000u) == 0x14000000u) return 1;         // b
    if ((w0 & 0xFFFFFC1Fu) == 0xD61F0000u) return 1;         // br xN
    if ((w0 & 0xFFFFFC1Fu) == 0xD63F0000u) return 1;         // blr xN
    if ((w0 & 0xFF000000u) == 0x58000000u &&
        (w1 & 0xFFFFFC1Fu) == 0xD61F0000u) return 1;         // ldr xN,#lit ; br xN
    return 0;
}

static int nk_hooked_libc(void) {
    static const char *syms[] = {
        "open", "read", "write", "close", "strstr", "strcmp", "strtol",
        "memcpy", "malloc", "pthread_create", "ptrace", "kill", NULL};
    for (int i = 0; syms[i]; i++) {
        void *f = dlsym(RTLD_DEFAULT, syms[i]);
        if (!f) continue;
        if (nk_looks_hooked((const unsigned char *)f)) return 1;
    }
    return 0;
}

// ---- 统一检查 ------------------------------------------------------------
static void nk_check_all(int first) {
    if (g_trap_seen) nk_die();

    int tp = nk_tracer_pid();
    if (tp == -2) return;              // 读不到就别误杀（沙盒/受限环境）
    if (g_wd_pid > 0) {
        if (tp != (int)g_wd_pid) nk_die();   // 被换 tracer / 被摘掉
    } else {
        if (tp != 0) nk_die();               // 没有看门狗时，任何 tracer 都是敌人
    }

    static int maps_on = -1;
    if (maps_on < 0) {
        const char *e = getenv("NK_AD_MAPS");
        maps_on = (e && e[0] == '1') ? 1 : 0;
    }
    if (maps_on && nk_anon_exec_segment()) nk_die();
    if (nk_hooked_libc()) nk_die();          // libc 被 inline hook（Frida 典型行为）
    if (nk_pending_trap()) nk_die();

    uint64_t h = nk_hash16((const void *)nk_decoy);
    if (g_prologue == 0)
        g_prologue = h;                  // 懒记录基线（避免构造期竞态）
    else if (first == 0 && h != g_prologue)
        nk_die();                        // 序言被 inline hook
}

// ---- 看门狗：占住 trace 位 ------------------------------------------------
static void nk_watchdog(pid_t target) {
    // 尝试 seize 父进程；成功则父进程 TracerPid = 本进程
    long r = ptrace(PTRACE_SEIZE, target, 0, 0);
    if (r != 0) {
        // seize 失败（受限内核 / 已被占用）：安静退出，
        // 让父进程退回“TracerPid 必须为 0”的普通模式，绝不误杀。
        _exit(0);
    }
    for (;;) {
        int st = 0;
        pid_t w = waitpid(target, &st, WNOHANG);
        if (w == target) _exit(0);       // 目标已退出
        if (kill(target, 0) != 0) _exit(0);
        usleep(200 * 1000);
    }
}

static void *nk_thread(void *arg) {
    (void)arg;
    // 给构造函数让出时间
    usleep(300 * 1000);
    int first = 1;
    while (!g_stop) {
        nk_check_all(first ? 1 : 0);
        first = 0;
        usleep(1200 * 1000);
    }
    return NULL;
}

__attribute__((constructor(101))) static void nk_ad_init(void) {
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);

    // 记录基线
    g_prologue = nk_hash16((const void *)nk_decoy);

    // 先做一次自检（此时还没有 tracer，正常应为 0）
    nk_check_all(1);

    pid_t self = getpid();
    pid_t wd = fork();
    if (wd == 0) {
        // 子进程：看门狗
        prctl(PR_SET_NAME, "nk-wd", 0, 0, 0);
        nk_watchdog(self);
        _exit(0);
    }
    // 等看门狗 seize，再确认它是否真的成了我们的 tracer
    usleep(120 * 1000);
    if (wd > 0) {
        int tp = nk_tracer_pid();
        g_wd_pid = (tp == (int)wd) ? wd : -1;
    }

    pthread_t th;
    if (pthread_create(&th, NULL, nk_thread, NULL) == 0)
        pthread_detach(th);
}
