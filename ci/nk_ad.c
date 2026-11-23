// nk_ad.c v8 —— Nk 反调试强化模块（arm64 / Android，纯用户态）
// v7 新增「模拟执行检测」：build.prop / 设备节点 / cpuinfo / auxv(vdso) / statfs(/proc)
//    / uname 一致性 / 无效系统调用语义 / cntvbr 与 CLOCK_MONOTONIC 比例 / SIGSEGV 投递
//    / 进程名回读 / 线程可见性。硬证据直接 _exit(97)，软证据只记日志（fail-open）。
// 原则：fail-open。读不到/权限不足一律放行，只在确定性证据上退出(_exit(97))。
//
// v6：NK_AD_OFF 只做「两处提前返回」(nk_die_r / __nk_ad_check)，文件其余部分
//     （构造函数、看门狗线程、各探测函数）照常编译链接。
//     教训：v5 把整个实现换成空函数后，产物在启动阶段 100% CPU 空转（0 字节输出）；
//     保持代码布局不变、只在运行时短路，才是可靠的「关闭反调试」。
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <stdint.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/ptrace.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <link.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>

/* 入口符号名由构建脚本随机指定（-DNK_AD_SYM=...），避免 .dynsym 里出现固定名字。
 * Pass 通过环境变量 NK_AD_SYM 拿到同一个名字，才能把 call 接到这里。 */
#ifndef NK_AD_SYM
#define NK_AD_SYM __nk_ad_check
#endif

#define NK_EXIT 97

static pid_t    g_wd_pid = -1;
static int      g_inited = 0;
static unsigned g_calls = 0;

#define NK_MAXMAP 768
typedef struct { uint64_t lo, hi; } nk_map_t;
static nk_map_t g_maps[NK_MAXMAP];
static int      g_nmaps = 0;

static uint64_t g_text_hash = 0;
static uint64_t g_text_addr = 0;
static uint64_t g_text_size = 0;
/* v10.1：我们自己把文本 mprotect 成 RWX 之后，不能在下一轮 maps 检查里
 * 把自己当成『文本被改写成可写』的攻击者，否则每次都会自杀。 */
static int g_text_rwx_self = 0;
static int g_wlock_tried = 0;

static long nk_sys(long n, long a, long b, long c);   /* fwd decl */
static long nk_sys5(long n, long a, long b, long c, long d, long e)
{
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    __asm__ __volatile__("svc #0"
                         : "+r"(x0)
                         : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4)
                         : "memory");
    return x0;
}
static int  nk_slurp(const char *path, char *buf, int cap); /* fwd decl */

static int nk_dbg_on(void) {
    static int cached = -1;
    if (cached < 0) cached = getenv("NK_AD_DEBUG") ? 1 : 0;
    return cached;
}
static void nk_dbg(const char *fmt, ...) {
    if (!nk_dbg_on()) return;
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    /* 同时写 stderr(2) 和 /data/local/tmp/nk_ad.log，避免任一通道被吞 */
    nk_sys(64, 2, (long)b, n);
    int fd = (int)nk_sys(56, -100, (long)"/data/local/tmp/nk_ad.log", 0x442 /* O_WRONLY|O_CREAT|O_APPEND */);
    if (fd >= 0) { nk_sys(64, fd, (long)b, n); nk_sys(57, fd, 0, 0); }
}

/* ===================== P-1 处置出口多态 =====================
 * 原来 30+ 个检测点全部汇到 nk_die_r 里的一句 _exit()，攻击者在 .text 里
 * 找到那一条 bl 改成 ret 就全线失效。这里把出口拆成三种语义不同的死法，
 * 每次按计数器轮换，静态上不存在「唯一处置点」这个 pattern。
 * 注意：spin 分支必须自带超时，绝不无限挂（v11.8 的教训）。 */
static unsigned g_panic_n = 0;

static void nk_panic_ill(void)  { __asm__ __volatile__("brk #0x1" ::: "memory"); }
static void nk_panic_exit(void) { nk_sys(94, NK_EXIT, 0, 0); }
static void nk_panic_spin(void) {
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &b);
        if ((b.tv_sec - a.tv_sec) >= 5) break;      /* 伪装 ANR，但只 5 秒 */
    }
    nk_sys(94, NK_EXIT, 0, 0);
}
static void nk_fatal_poly(void) {
    unsigned n = (unsigned)__atomic_add_fetch(&g_panic_n, 1, __ATOMIC_RELAXED);
    switch (n % 3u) {
        case 0:  nk_panic_ill();  break;
        case 1:  nk_panic_spin(); break;
        default: nk_panic_exit(); break;
    }
    nk_sys(94, NK_EXIT, 0, 0);
    for (;;) nk_sys(94, NK_EXIT, 0, 0);
}

static void nk_die_r(const char *why) {
#ifdef NK_AD_OFF
    (void)why;
    return;   /* 关闭反调试：任何判定都不生效 */
#endif
    {
        /* 退出原因无论调试开关都留痕：写 stderr + 追加 nk_ad.log，
         * 这样用户看得见「为什么 97」，也方便远程定位。 */
        char line[256];
        int ln = snprintf(line, sizeof line, "nk_ad[die] reason=%s\n", why);
        if (ln > 0) {
            nk_sys(64, 2, (long)line, ln);
            long fd = nk_sys(56, (long)"/data/local/tmp/nk_ad.log",
                             0x441 /* O_WRONLY|O_CREAT|O_APPEND */, 0644);
            if (fd >= 0) {
                nk_sys(64, fd, (long)line, ln);
                nk_sys(57, fd, 0, 0);
            }
        }
    }
    nk_fatal_poly();      /* P-1：多态出口 */
}
static void nk_die(void) { nk_die_r("nk_ad: unknown"); }
/* 判断某个 pid 的 comm 是否为我们的看门狗线程名（nk-wd）。
   用 comm 而不是 pid 比较，避免 PTRACE_SEIZE 完成时序不同导致误判。 */
static int nk_comm_is(pid_t pid, const char *want) {
    char path[64], b[80];
    snprintf(path, sizeof path, "/proc/%d/comm", (int)pid);
    if (nk_slurp(path, b, sizeof b) <= 0) return 0;
    char *nl = strchr(b, '\n');
    if (nl) *nl = 0;
    return strcmp(b, want) == 0;
}

/* 是否存在同时可写可执行(rwx)的映射：正常 App 极少出现，是内存补丁的强特征 */
static int nk_has_rwx(void) {
    char *buf = (char *)malloc(256 * 1024);
    if (!buf) return 0;
    int n = nk_slurp("/proc/self/maps", buf, 256 * 1024);
    int hit = 0;
    if (n > 0) {
        char *q = buf;
        while (q && *q) {
            char *nl = strchr(q, '\n');
            if (nl) *nl = 0;
            unsigned long lo = 0, hi = 0;
            char perm[8] = {0};
            if (sscanf(q, "%lx-%lx %7s", &lo, &hi, perm) == 3 &&
                perm[0] == 'r' && perm[1] == 'w' && perm[2] == 'x') {
                /* v10.2：这块 RWX 如果是我们自己给文本打的，就别自杀 */
                if (g_text_rwx_self && g_text_addr >= (uint64_t)lo &&
                    g_text_addr < (uint64_t)hi) {
                    q = nl ? nl + 1 : NULL;
                    continue;
                }
                nk_dbg("nk_ad[rwx] %lx-%lx\n", lo, hi);
                hit = 1;
                break;
            }
            q = nl ? nl + 1 : NULL;
        }
    }
    free(buf);
    return hit;
}

static long nk_sys(long n, long a, long b, long c) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    __asm__ __volatile__("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}
static int  nk_open_ro(const char *p) { return (int)nk_sys(56, -100, (long)p, 0); }
static long nk_read_fd(int fd, void *b, long n) { return nk_sys(63, fd, (long)b, n); }
static long nk_close_fd(int fd) { return nk_sys(57, fd, 0, 0); }

static int nk_slurp(const char *path, char *buf, int cap) {
    int fd = nk_open_ro(path);
    if (fd < 0) return -1;
    int total = 0;
    while (total < cap - 1) {
        long r = nk_read_fd(fd, buf + total, cap - 1 - total);
        if (r <= 0) break;
        total += (int)r;
    }
    nk_close_fd(fd);
    buf[total] = 0;
    return total;
}

static int nk_parse_maps(nk_map_t *out, int cap) {
    char *buf = (char *)malloc(256 * 1024);
    if (!buf) return -1;
    int n = nk_slurp("/proc/self/maps", buf, 256 * 1024);
    if (n <= 0) { free(buf); return -1; }
    int cnt = 0;
    char *p = buf;
    while (p && *p && cnt < cap) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        unsigned long lo = 0, hi = 0;
        char perm[8] = {0};
        if (sscanf(p, "%lx-%lx %7s", &lo, &hi, perm) == 3) {
            if (perm[2] == 'x') { out[cnt].lo = lo; out[cnt].hi = hi; cnt++; }
        }
        p = nl ? nl + 1 : NULL;
    }
    free(buf);
    return cnt;
}

static int nk_region_known(uint64_t lo, uint64_t hi) {
    for (int i = 0; i < g_nmaps; i++)
        if (g_maps[i].lo == lo && g_maps[i].hi == hi) return 1;
    return 0;
}

// bionic 里有大量合法 thunk（例如 open -> b openat），所以不能一看到 b 就判 hook。
// 改为：解析 b 的立即数，只有“跳到本模块之外”才算 inline hook（Frida 的 trampoline
// 通常在独立映射里）。
static int nk_hooked_at_in(const unsigned char *p, uint64_t mod_lo, uint64_t mod_hi) {
    unsigned w0 = 0, w1 = 0;
    memcpy(&w0, p, 4);
    memcpy(&w1, p + 4, 4);

    if ((w0 & 0xFC000000u) == 0x14000000u) {           // b <imm26>
        int32_t imm = (int32_t)(w0 & 0x03FFFFFFu);
        if (imm & 0x02000000) imm |= (int32_t)0xFC000000u;   // sign extend
        uint64_t tgt = (uint64_t)((const unsigned char *)p + (int64_t)imm * 4);
        if (tgt < mod_lo || tgt >= mod_hi) return 1;   // 跳出模块 → 可疑
        return 0;                                       // 模块内 thunk → 正常
    }
    if ((w0 & 0xFFFFFC1Fu) == 0xD61F0000u) return 1;   // br xN
    if ((w0 & 0xFFFFFC1Fu) == 0xD63F0000u) return 1;   // blr xN
    if ((w0 & 0xFF000000u) == 0x58000000u &&
        (w1 & 0xFFFFFC1Fu) == 0xD61F0000u) return 1;   // ldr xN,#lit ; br xN
    return 0;
}

static int nk_hooked_libc(void) {
    static const char *syms[] = {"open", "read", "write", "close", "strstr",
                                 "strcmp", "strtol", "memcpy", "malloc",
                                 "pthread_create", "ptrace", "kill", "execve", NULL};
    for (int i = 0; syms[i]; i++) {
        void *f = dlsym(RTLD_DEFAULT, syms[i]);
        if (!f) continue;
        Dl_info di;
        if (dladdr(f, &di) == 0 || !di.dli_fbase) continue;
        uint64_t lo = (uint64_t)di.dli_fbase;
        uint64_t hi = lo + (16u << 20);          // libc 大小上限，粗范围
        if (nk_hooked_at_in((const unsigned char *)f, lo, hi)) return 1;
    }
    return 0;
}

static int nk_any_thread_traced(void)
{
    if (getenv("NK_AD_ALLOW_DEBUG")) return 0;
    int fd = nk_open_ro("/proc/self/task");
    if (fd < 0) return 0;
    char dbuf[4096];
    for (;;) {
        long r = nk_sys(61, fd, (long)dbuf, (long)sizeof dbuf);
        if (r <= 0) break;
        long off = 0;
        while (off + 19 < r) {
            unsigned short reclen = *(unsigned short *)(dbuf + off + 16);
            char *name = dbuf + off + 19;
            if (name[0] >= '1' && name[0] <= '9') {
                char path[128], st[4096];
                snprintf(path, sizeof path, "/proc/self/task/%s/status", name);
                if (nk_slurp(path, st, sizeof st) > 0) {
                    char *t = strstr(st, "TracerPid:");
                    if (t) {
                        int pid = (int)strtol(t + 10, NULL, 10);
                        if (pid != 0 && !(g_wd_pid > 0 && pid == (int)g_wd_pid)) {
                            nk_dbg("nk_ad[task] %s traced by %d\n", name, pid);
                            nk_close_fd(fd); return 1;
                        }
                    }
                }
            }
            if (reclen < 24) break;   /* P3：畸形记录直接放弃，防死循环 */
            off += reclen;
        }
    }
    nk_close_fd(fd);
    return 0;
}

static uint64_t nk_hash_mem(const unsigned char *p, uint64_t n) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (uint64_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ULL; }
    return h;
}

struct nk_ctx { uint64_t a, s; int done; };
static int nk_text_cb(struct dl_phdr_info *info, size_t sz, void *data) {
    (void)sz;
    struct nk_ctx *c = (struct nk_ctx *)data;
    if (info->dlpi_name && info->dlpi_name[0] != 0) return 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type == PT_LOAD && (ph->p_flags & PF_X)) {
            c->a = (uint64_t)info->dlpi_addr + ph->p_vaddr;
            c->s = ph->p_memsz > (1u << 20) ? (uint64_t)(1u << 20) : (uint64_t)ph->p_memsz;
            c->done = 1;
            return 1;
        }
    }
    return 0;
}


/* ================= 模拟执行 / 沙箱 / 模拟器检测（v7） =================
 * 目标：unidbg / Qiling / Unicorn / Frida-模拟 CPU / 各类云手机与模拟器。
 * 判定原则：
 *   硬证据（命中即 _exit(97)）：build.prop 里的 qemu 标记、qemu 设备节点、
 *     cpuinfo implementer=0x00、无 vDSO 的 auxv、/proc 文件系统魔数不对、
 *     uname 与 /proc/sys/kernel/osrelease 不一致、无效系统调用语义异常。
 *   软证据（只记分，不杀）：cntvct 比例异常、SIGSEGV 未投递、进程名回读失败、
 *     线程不可见、/proc/self/maps 缺 [stack]。软证据 ≥3 项也杀。
 */

static int g_emu_score = 0;     /* 只装「真·模拟器证据」，>=3 判死 */
static int g_tool_score = 0;    /* 只装「工具指纹 / 环境信息」，永远不判死，仅日志 */
/* JNI 表检查的软分只记一次：真机上这条可能稳定误报（JIT 指针），
 * 而 worker 每 1.5s 一次深检，若每次加分，4~6 秒就撞上 g_emu_score>=3 自杀。 */
static int g_jni_soft_done = 0;

static int nk_emu_on(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *e = getenv("NK_AD_EMU");
        cached = (e && e[0] == '0') ? 0 : 1;
    }
    return cached;
}

static void nk_soft(const char *tag) {
    g_emu_score++;
    nk_dbg("nk_ad[emu] soft %s (score=%d)\n", tag, g_emu_score);
}

/* v11.2 P2：工具指纹类软分单独计数，绝不参与判死。
 * 之前 xposed-dir-in-rooted 走 nk_soft，root 设备（KernelSU/Magisk + LSPosed）
 * 每次深检都命中 → 1.5s 一次 → 4~6 秒自己把自己杀了。 */
static void nk_soft_tool(const char *tag) {
    /* v11.3 P2：饱和计数，避免长时间运行无界增长（只影响日志观感） */
    if (g_tool_score < 255) g_tool_score++;
    nk_dbg("nk_ad[tool] soft %s (tool=%d, 仅日志)\n", tag, g_tool_score);
}

/* ---------- 分块源码：按原顺序 include，单 TU 等价 ---------- */

/* ---------- 分块源码：按原顺序 include，单 TU 等价 ---------- */

/* ---------- 分块源码：按原顺序 include，单 TU 等价 ---------- */
#include "nk_ad_parts/01_nk_emu_prop.inc"
#include "nk_ad_parts/02_nk_emu_syscall.inc"
#include "nk_ad_parts/03_nk_emu_maps.inc"
#include "nk_ad_parts/04_nk_jni_table_ok.inc"
#include "nk_ad_parts/05_nk_behavior_scan.inc"
#include "nk_ad_parts/06_nk_proc_starttime.inc"
#include "nk_ad_parts/07_nk_cross_conflict.inc"
#include "nk_ad_parts/08_nk_syscall_semantic.inc"
