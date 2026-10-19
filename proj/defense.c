/* ============================================================================
 * defense.c —— 运行时自保护实现（纯 C，无 libc++ 依赖）
 * ----------------------------------------------------------------------------
 * 不含任何对外部工程的依赖；只用 bionic 的基本接口。
 * 构造期自动启动一个 detached watch 线程，周期巡检。
 * ========================================================================== */
#define _GNU_SOURCE
#include "defense.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <sys/mman.h>

#ifndef DEFENSE_LEVEL
#define DEFENSE_LEVEL 3
#endif
#ifndef DEFENSE_INTERVAL_MS
#define DEFENSE_INTERVAL_MS 2000
#endif
#ifndef DEFENSE_LOG
#define DEFENSE_LOG 0
#endif
#ifndef DEFENSE_PROCESS_NAME
#define DEFENSE_PROCESS_NAME "media.codec"
#endif

/* ------------------------------------------------------------------------- */
static void dlog(const char *msg)
{
#if DEFENSE_LOG
    printf("[defense] %s\n", msg);
    fflush(stdout);
#else
    (void)msg;
#endif
}

void defense_panic(void)
{
    /* 静默退出：不给分析者任何提示，也不跑 atexit 回调 */
    _exit(0);
}

/* ============================ 代码段 CRC ================================= */
static uint32_t crc_table[256];
static int crc_ready = 0;

static void crc_init(void)
{
    uint32_t c;
    int i, k;
    for (i = 0; i < 256; i++) {
        c = (uint32_t)i;
        for (k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
    crc_ready = 1;
}

static uint32_t crc32_buf(const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    if (!crc_ready) crc_init();
    for (i = 0; i < len; i++) c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static unsigned long g_text_start = 0;
static unsigned long g_text_end = 0;
static uint32_t g_text_crc = 0;
static int g_text_ok = 0;

/* 找到本可执行文件的第一个可执行映射（r-xp）区间 */
static int defense_locate_text(void)
{
    FILE *f = fopen("/proc/self/maps", "r");
    char line[1024];
    char self[512];
    ssize_t n;
    if (!f) return -1;

    n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n <= 0) { fclose(f); return -1; }
    self[n] = '\0';

    while (fgets(line, sizeof(line), f)) {
        unsigned long a = 0, b = 0;
        char perms[8] = {0};
        char path[512] = {0};
        /* 格式： start-end perms offset dev inode path */
        if (sscanf(line, "%lx-%lx %7s %*s %*s %*s %511s", &a, &b, perms, path) != 4)
            continue;
        if (strchr(perms, 'x') && strncmp(path, self, strlen(self)) == 0) {
            g_text_start = a;
            g_text_end = b;
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return -1;
}

#if DEFENSE_LEVEL >= 3
static int defense_text_baseline(void)
{
    if (defense_locate_text() != 0) return -1;
    if (g_text_end <= g_text_start) return -1;
    g_text_crc = crc32_buf((const void *)g_text_start, (size_t)(g_text_end - g_text_start));
    g_text_ok = 1;
    dlog("text baseline captured");
    return 0;
}

static int defense_text_verify(void)
{
    uint32_t now;
    if (!g_text_ok) return 0;
    now = crc32_buf((const void *)g_text_start, (size_t)(g_text_end - g_text_start));
    if (now != g_text_crc) {
        dlog("text crc mismatch (runtime patch)");
        defense_panic();
        return -1;
    }
    return 0;
}
#endif

/* ============================ 反调试 ==================================== */
/* 读 TracerPid（主线程与各线程都看一遍） */
static int defense_tracepid_hit(void)
{
    FILE *f;
    char line[256];
    int tid_pid = 0;

    f = fopen("/proc/self/status", "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "TracerPid:", 10) == 0) {
                tid_pid = atoi(line + 10);
                break;
            }
        }
        fclose(f);
    }
    if (tid_pid != 0) { dlog("TracerPid != 0"); return 1; }

    /* 逐线程检查（有的注入只 attach 单线程） */
    {
        DIR *d = opendir("/proc/self/task");
        struct dirent *e;
        if (d) {
            while ((e = readdir(d)) != NULL) {
                char p[128];
                if (!isdigit((unsigned char)e->d_name[0])) continue;
                snprintf(p, sizeof(p), "/proc/self/task/%s/status", e->d_name);
                f = fopen(p, "r");
                if (!f) continue;
                while (fgets(line, sizeof(line), f)) {
                    if (strncmp(line, "TracerPid:", 10) == 0) {
                        if (atoi(line + 10) != 0) { fclose(f); closedir(d); dlog("thread traced"); return 1; }
                        break;
                    }
                }
                fclose(f);
            }
            closedir(d);
        }
    }
    return 0;
}

/* ============================ 特征库扫描 ================================= */
static const char *const kBadTokens[] = {
    "frida", "gadget", "substrate", "xposed", "lsposed", "edxposed",
    "magisk", "gdbserver", "libdbg", "lldb",
    "re.frida.server", "libfrida", "frida-agent", "frida-gadget",
    "dumpdecrypted", "objection", "bfinject",
    NULL
};

/* 返回 1 命中 */
static int defense_maps_scan(void)
{
    FILE *f = fopen("/proc/self/maps", "r");
    char line[1024];
    int i;
    if (!f) return 0;
    while (fgets(line, sizeof(line), f)) {
        /* 只关心有路径的行（匿名映射不匹配） */
        for (i = 0; kBadTokens[i]; i++) {
            if (strstr(line, kBadTokens[i]) != NULL) {
                fclose(f);
                dlog("maps token hit");
                return 1;
            }
        }
    }
    fclose(f);
    return 0;
}

/* ============================ 反 inline hook ============================ */
/* 看 libc 函数序言是否被改成 B / LDR-br 跳板（arm64） */
static int defense_prologue_hooked(const void *fn)
{
    uint32_t w = 0;
    if (!fn) return 0;
    memcpy(&w, fn, sizeof(w));
#if defined(__aarch64__)
    if ((w & 0xFC000000u) == 0x14000000u) return 1;  /* B imm26 */
    if ((w & 0xFF000000u) == 0x58000000u) return 1;  /* LDR x16, #imm */
#elif defined(__arm__)
    if ((w & 0x0F000000u) == 0x0A000000u) return 1;  /* B */
#elif defined(__x86_64__) || defined(__i386__)
    if ((w & 0xFFu) == 0xE9u) return 1;              /* JMP rel32 */
    if ((w & 0xFFFFu) == 0x25FFu) return 1;          /* JMP [rip+x] */
#endif
    return 0;
}

static int defense_hook_scan(void)
{
    /* 只用 bionic 里没有 __overloadable/fortify 包装的函数，避免取址歧义 */
    if (defense_prologue_hooked((const void *)&fopen))   { dlog("fopen hooked");   return 1; }
    if (defense_prologue_hooked((const void *)&fgets))   { dlog("fgets hooked");   return 1; }
    if (defense_prologue_hooked((const void *)&fclose))  { dlog("fclose hooked");  return 1; }
    if (defense_prologue_hooked((const void *)&opendir)) { dlog("opendir hooked"); return 1; }
    return 0;
}

/* ============================ 时序异常（单步/插桩）====================== */
#if DEFENSE_LEVEL >= 3
static int defense_timing_anomaly(void)
{
    struct timespec t0, t1;
    volatile uint32_t acc = 0;
    uint32_t i;
    long ms;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < 200000u; i++) acc += (i ^ (acc >> 3));
    clock_gettime(CLOCK_MONOTONIC, &t1);

    ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    /* 正常 <20ms；单步/插桩/模拟器会慢一个数量级 */
    if (ms > 800) {
        dlog("timing anomaly");
        return 1;
    }
    return 0;
}
#endif

/* ============================ 巡检 ====================================== */
int defense_check_once(void)
{
    if (DEFENSE_LEVEL <= 0) return 0;
#if DEFENSE_LEVEL >= 2
    if (defense_tracepid_hit()) return 1;
    if (defense_maps_scan())    return 1;
    if (defense_hook_scan())    return 1;
#endif
#if DEFENSE_LEVEL >= 3
    if (defense_text_verify() == -1) return 1;
    if (defense_timing_anomaly()) return 1;
#endif
    return 0;
}

/* ============================ 初始化 ==================================== */
static void *defense_watch(void *arg)
{
    (void)arg;
    for (;;) {
        if (defense_check_once() != 0) defense_panic();
        {
            struct timespec ts;
            ts.tv_sec = DEFENSE_INTERVAL_MS / 1000;
            ts.tv_nsec = (long)(DEFENSE_INTERVAL_MS % 1000) * 1000000L;
            nanosleep(&ts, NULL);
        }
    }
    return NULL;
}

int defense_init(void)
{
    if (DEFENSE_LEVEL <= 0) return 0;

#if DEFENSE_LEVEL >= 1
    /* 不可被 dump/attach 的核心设置 */
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
    {
        struct rlimit rl;
        rl.rlim_cur = 0;
        rl.rlim_max = 0;
        setrlimit(RLIMIT_CORE, &rl);
    }
    /* 自我 trace：别人再 attach 会失败 */
    ptrace(PTRACE_TRACEME, 0, NULL, NULL);
#endif

    if (DEFENSE_PROCESS_NAME[0] != '\0')
        prctl(PR_SET_NAME, (unsigned long)DEFENSE_PROCESS_NAME, 0, 0, 0);

#if DEFENSE_LEVEL >= 3
    defense_text_baseline();
#endif

    /* 启动首次巡检 + 后台线程 */
    if (defense_check_once() != 0) defense_panic();
    {
        pthread_t th;
        pthread_attr_t at;
        if (pthread_attr_init(&at) == 0) {
            pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
            if (pthread_create(&th, &at, defense_watch, NULL) != 0) {
                /* 起不来线程（极少见）就只保留构造期检查 */
            }
            pthread_attr_destroy(&at);
        }
    }
    dlog("defense armed");
    return 0;
}

void defense_status(void)
{
    printf("[defense] level=%d interval=%dms process_name=%s text_crc=%s\n",
           DEFENSE_LEVEL, DEFENSE_INTERVAL_MS,
           DEFENSE_PROCESS_NAME[0] ? DEFENSE_PROCESS_NAME : "(unchanged)",
           g_text_ok ? "on" : "off");
}

/* ============================ 字符串解码（配合 defense_str.h）============ */
const char *defense_str_dec(const unsigned char *blob, size_t len, unsigned char key)
{
    static __thread char buf[4][512];
    static __thread int idx = 0;
    char *out = buf[idx];
    size_t n, i;

    idx = (idx + 1) & 3;
    n = (len < sizeof(buf[0])) ? len : (sizeof(buf[0]) - 1);
    for (i = 0; i < n; i++) out[i] = (char)(blob[i] ^ key);
    out[n] = '\0';
    return out;
}

/* 构造期自动启动（不需要改 main） */
__attribute__((constructor))
static void defense_ctor(void)
{
    defense_init();
}
