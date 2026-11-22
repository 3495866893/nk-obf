// nk_ad.c v8 —— Ensia 反调试强化模块（arm64 / Android，纯用户态）
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
#include <stdarg.h>
#include <sys/ptrace.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <link.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <sys/stat.h>

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

static long nk_sys(long n, long a, long b, long c);   /* fwd decl */
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
static void nk_die_r(const char *why) {
#ifdef NK_AD_OFF
    (void)why;
    return;   /* 关闭反调试：任何判定都不生效 */
#endif
    if (getenv("NK_AD_DEBUG")) {
        // 直连 write(2, ...)，避免调试输出本身被 hook
        int n = (int)strlen(why);
        long r = nk_sys(64, 2, (long)why, n);
        (void)r;
        const char *nl = "\n";
        nk_sys(64, 2, (long)nl, 1);
    }
    _exit(NK_EXIT);
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

static int nk_any_thread_traced(void) {
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
                        if (pid != 0 && !nk_comm_is((pid_t)pid, "nk-wd")) {
                            nk_dbg("nk_ad[task] %s traced by %d\n", name, pid);
                            nk_close_fd(fd); return 1;
                        }
                    }
                }
            }
            if (reclen == 0) break;
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

static int g_emu_score = 0;

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

/* ---- 1. build.prop：qemu / SDK 型号 / goldfish 硬件 ---- */
static int nk_emu_prop(void) {
    static const char *paths[] = {
        "/system/build.prop", "/vendor/build.prop", "/product/build.prop",
        "/default.prop", "/odm/etc/build.prop", "/system/etc/build.prop",
    };
    static const char *bad[] = {
        "ro.kernel.qemu=1", "ro.kernel.qemu=2", "ro.boot.qemu=1",
        "ro.hardware=goldfish", "ro.hardware=ranchu", "ro.hardware=qemu",
        "ro.product.model=Android SDK", "ro.product.name=sdk",
        "ro.product.device=generic", "ro.build.flavor=sdk_",
        "init.svc.qemud=", "ro.bootloader=unknown",
    };
    char buf[8192];
    for (int i = 0; i < (int)(sizeof paths / sizeof paths[0]); i++) {
        int n = nk_slurp(paths[i], buf, (int)sizeof buf - 1);
        if (n <= 0) continue;
        buf[n] = 0;
        for (int j = 0; j < (int)(sizeof bad / sizeof bad[0]); j++)
            if (strstr(buf, bad[j])) {
                nk_dbg("nk_ad[emu] prop %s -> %s\n", paths[i], bad[j]);
                return 1;
            }
    }
    return 0;
}

/* ---- 2. qemu / goldfish 设备节点与驱动库 ---- */
static int nk_emu_dev(void) {
    static const char *nodes[] = {
        "/dev/qemu_pipe", "/dev/qemu_trace", "/dev/goldfish_pipe",
        "/dev/qemu_events", "/dev/socket/qemud", "/system/bin/qemud",
        "/system/lib/libc_malloc_debug_qemu.so",
        "/system/lib64/libc_malloc_debug_qemu.so",
        "/sys/qemu_trace", "/proc/tty/drivers/goldfish",
    };
    for (int i = 0; i < (int)(sizeof nodes / sizeof nodes[0]); i++) {
        /* access(path, F_OK) = syscall 48 */
        if (nk_sys(48, (long)nodes[i], 0, 0) == 0) {
            nk_dbg("nk_ad[emu] node %s\n", nodes[i]);
            return 1;
        }
    }
    return 0;
}

/* ---- 3. cpuinfo：qemu 的 CPU implementer 是 0x00 ---- */
static int nk_emu_cpu(void) {
    char buf[8192];
    int n = nk_slurp("/proc/cpuinfo", buf, (int)sizeof buf - 1);
    if (n <= 0) return 0;
    buf[n] = 0;
    const char *p = buf;
    int impl_zero = 0, seen_impl = 0, goldfish = 0;
    while (p && *p) {
        if (strncmp(p, "CPU implementer", 15) == 0) {
            const char *c = strchr(p, ':');
            if (c) {
                seen_impl = 1;
                if (strstr(c, "0x00") || strstr(c, "0x0 ")) impl_zero = 1;
            }
        }
        if (strstr(p, "goldfish") || strstr(p, "ranchu")) { goldfish = 1; break; }
        p = strchr(p, '\n');
        if (p) p++;
    }
    if (goldfish) { nk_dbg("nk_ad[emu] cpuinfo goldfish\n"); return 1; }
    /* 真机一定是 0x41(ARM)/0x48/0x51/0x61；0x00 只出现在纯软件模拟里 */
    if (seen_impl && impl_zero) { nk_dbg("nk_ad[emu] cpuinfo implementer=0x00\n"); return 1; }
    return 0;
}

/* ---- 4. auxv：没有 vDSO / HWCAP 为 0 = 模拟 ---- */
static int nk_emu_auxv(void) {
    int fd = nk_open_ro("/proc/self/auxv");
    if (fd < 0) return 0;
    unsigned long long e[128 * 2];
    long n = nk_read_fd(fd, e, (long)sizeof e);
    nk_close_fd(fd);
    if (n <= 0) return 0;
    int cnt = (int)(n / (long)sizeof(unsigned long long) / 2);
    unsigned long long sysinfo = 0, hwcap = 0, platform = 0, random = 0;
    for (int i = 0; i < cnt; i++) {
        unsigned long long t = e[i * 2], v = e[i * 2 + 1];
        if (t == 0) break;
        if (t == 33) sysinfo = v;   /* AT_SYSINFO_EHDR */
        if (t == 16) hwcap = v;     /* AT_HWCAP */
        if (t == 15) platform = v;  /* AT_PLATFORM */
        if (t == 25) random = v;    /* AT_RANDOM */
    }
    if (sysinfo == 0) { nk_dbg("nk_ad[emu] auxv no vDSO\n"); return 1; }
    if (hwcap == 0)   { nk_dbg("nk_ad[emu] auxv hwcap=0\n"); return 1; }
    if (platform == 0 || random == 0) nk_soft("auxv-platform");
    return 0;
}

/* ---- 5. /proc 必须是 procfs（f_type = PROC_SUPER_MAGIC） ---- */
static int nk_emu_procfs(void) {
    unsigned long long st[32];          /* struct statfs 在 arm64 上 ~120B */
    memset(st, 0, sizeof st);
    long r = nk_sys(43 /* statfs */, (long)"/proc", (long)st, 0);
    if (r != 0) return 0;                       /* fail-open */
    unsigned long long magic = st[0];           /* f_type 在首位 */
    if (magic == 0) { nk_dbg("nk_ad[emu] procfs magic=0\n"); return 1; }
    if (magic != 0x9fa0ULL) {
        nk_dbg("nk_ad[emu] procfs magic=%llx\n", magic);
        /* 有些内核报 0x9fa0 的高位扩展，只认低 16 位 */
        if ((magic & 0xFFFFULL) != 0x9fa0ULL) return 1;
    }
    return 0;
}

/* ---- 6. uname 与 /proc/sys/kernel/osrelease 必须一致，machine 必须 aarch64 ---- */
struct nk_uts { char sys[65], node[65], rel[65], ver[65], mach[65], dom[65]; };

static int nk_emu_uname(void) {
    struct nk_uts u;
    memset(&u, 0, sizeof u);
    if (nk_sys(160 /* uname */, (long)&u, 0, 0) != 0) return 0;
    if (u.mach[0] && strcmp(u.mach, "aarch64") != 0) {
        nk_dbg("nk_ad[emu] uname machine=%s\n", u.mach);
        return 1;
    }
    char os[128];
    int n = nk_slurp("/proc/sys/kernel/osrelease", os, (int)sizeof os - 1);
    if (n > 0) {
        while (n > 0 && (os[n - 1] == '\n' || os[n - 1] == '\r')) os[--n] = 0;
        if (u.rel[0] && strcmp(u.rel, os) != 0) {
            nk_dbg("nk_ad[emu] uname rel=%s vs proc=%s\n", u.rel, os);
            return 1;
        }
    }
    return 0;
}

/* ---- 7. 无效系统调用必须回 ENOSYS(38)；getpid/gettid 与 /proc 一致 ---- */
static int nk_emu_syscall(void) {
    long bad = nk_sys(999999, 0, 0, 0);           /* 不存在的调用号 */
    if (bad == 0 || bad > 0) {
        nk_dbg("nk_ad[emu] bogus syscall returned %ld\n", bad);
        return 1;
    }
    long pid = nk_sys(172 /* getpid on arm64 */, 0, 0, 0);
    char st[2048];
    int n = nk_slurp("/proc/self/status", st, (int)sizeof st - 1);
    if (n > 0) {
        st[n] = 0;
        char *t = strstr(st, "Pid:");
        if (t) {
            long real = strtol(t + 4, NULL, 10);
            if (pid > 0 && real > 0 && pid != real) {
                nk_dbg("nk_ad[emu] getpid=%ld vs proc=%ld\n", pid, real);
                return 1;
            }
        }
    }
    return 0;
}

/* ---- 8. cntvct 与 CLOCK_MONOTONIC 比例（模拟器常给死计数器） ---- */
static int nk_emu_timer(void) {
    unsigned long long frq = 0, t0 = 0, t1 = 0;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(frq));
    if (frq == 0 || frq < 1000000ULL || frq > 5000000000ULL) {
        nk_dbg("nk_ad[emu] cntfrq=%llu\n", frq);
        nk_soft("cntfrq");
        return 0;
    }
    struct timespec a, b;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(t0));
    clock_gettime(CLOCK_MONOTONIC, &a);
    usleep(20000);
    clock_gettime(CLOCK_MONOTONIC, &b);
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(t1));

    double wall = (double)(b.tv_sec - a.tv_sec) + (double)(b.tv_nsec - a.tv_nsec) / 1e9;
    double hw   = (double)(t1 - t0) / (double)frq;
    if (wall > 0.002) {
        double ratio = hw / wall;
        nk_dbg("nk_ad[emu] timer wall=%.4f hw=%.4f ratio=%.3f\n", wall, hw, ratio);
        /* 真机 0.9~1.1；模拟器要么 0（计数不动）要么差一个数量级 */
        if (ratio < 0.25 || ratio > 4.0) nk_soft("timer-ratio");
    }
    return 0;
}

/* ---- 9. SIGSEGV 必须被投递，且 si_addr 指向被访问的那页 ---- */
static sigjmp_buf g_emu_jb;
static volatile unsigned long long g_emu_page;
static volatile int g_emu_kind;   /* 0=没收到 1=收到了但地址不对 2=正确 */

static void nk_emu_segv(int sig, siginfo_t *si, void *uc) {
    (void)sig; (void)uc;
    g_emu_kind = (si && (unsigned long long)(uintptr_t)si->si_addr == g_emu_page) ? 2 : 1;
    siglongjmp(g_emu_jb, 1);
}

static int nk_emu_trap(void) {
    void *p = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return 0;
    struct sigaction sa, old;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = nk_emu_segv;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, &old) != 0) { munmap(p, 4096); return 0; }

    g_emu_page = (unsigned long long)(uintptr_t)p;
    g_emu_kind = 0;
    if (sigsetjmp(g_emu_jb, 1) == 0) {
        volatile unsigned char c = *(volatile unsigned char *)p;   /* 故意越权读 */
        (void)c;
    }
    sigaction(SIGSEGV, &old, NULL);
    munmap(p, 4096);

    nk_dbg("nk_ad[emu] segv kind=%d\n", g_emu_kind);
    if (g_emu_kind == 0) nk_soft("segv-not-delivered");
    else if (g_emu_kind == 1) { nk_dbg("nk_ad[emu] segv si_addr wrong\n"); return 1; }
    return 0;
}

/* ---- 10. 进程名回读：prctl 设的名字必须出现在 /proc/self/comm ---- */
static int nk_emu_name(void) {
    char old[17];
    memset(old, 0, sizeof old);
    if (prctl(PR_GET_NAME, (unsigned long)old, 0, 0, 0) != 0) return 0;
    const char *probe = "nk-ad-chk";
    if (prctl(PR_SET_NAME, (unsigned long)probe, 0, 0, 0) != 0) return 0;
    char c[64];
    int n = nk_slurp("/proc/self/comm", c, (int)sizeof c - 1);
    prctl(PR_SET_NAME, (unsigned long)old, 0, 0, 0);
    if (n <= 0) return 0;                     /* fail-open */
    c[n] = 0;
    if (strncmp(c, probe, 9) != 0) {
        nk_dbg("nk_ad[emu] comm=%s\n", c);
        nk_soft("comm-readback");
    }
    return 0;
}

/* ---- 11. 线程可见性：我们开了看门狗线程，/proc/self/task 至少 2 个 ---- */
static int nk_emu_task(void) {
    int fd = nk_open_ro("/proc/self/task");
    if (fd < 0) return 0;
    char buf[2048];
    long n = nk_read_fd(fd, buf, (long)sizeof buf - 1);
    nk_close_fd(fd);
    if (n <= 0) return 0;
    buf[n] = 0;
    int cnt = 0;
    for (long i = 0; i < n; i++) if (buf[i] == '\n') cnt++;
    nk_dbg("nk_ad[emu] task entries=%d\n", cnt);
    if (cnt <= 1) nk_soft("task-invisible");
    return 0;
}

/* ---- 12. maps 里必须有 [stack]；有 vDSO 就必须有 [vdso] ---- */
static int nk_emu_maps(void) {
    char buf[16384];
    int n = nk_slurp("/proc/self/maps", buf, (int)sizeof buf - 1);
    if (n <= 0) return 0;
    buf[n] = 0;
    if (!strstr(buf, "[stack]")) nk_soft("no-stack");
    if (!strstr(buf, "[vdso]"))  nk_soft("no-vdso");
    return 0;
}


/* ================= 调试工具指纹检测（v8） =================
 * 覆盖：Frida（含 phantom-frida 打过补丁的残余特征）、Xposed/LSPosed/Zygisk、
 *       ptrace/IDA、eBPF uprobe(edbg/stackplz) 的断点改写、QBDI/Unicorn 痕迹。
 * 注意：本机是 KernelSU（/data/adb 合法存在），所有 root 相关只记软分。
 */

/* maps 里不能出现的模块名（hard） */
static const char *g_bad_maps[] = {
    "frida-agent", "frida-gadget", "libgum", "gum-js-loop", "frida-server",
    "linjector", "re.frida", "lsposed", "libxposed", "xposedbridge",
    "zygisk_lsposed", "riru_", "edbg", "stackplz", "qbdi", "libqbdi",
    "unicorntrace", "libunicorn", "substrate", "libhoudini_frida",
};

/* 线程名（Frida 运行时线程 + 常见注入器） */
static const char *g_bad_comm[] = {
    "gum-js-loop", "gmain", "gdbus", "pool-frida", "frida", "linjector",
    "artseek", "qbdi", "edbg",
};

static int nk_maps_hit(void) {
    char *buf = (char *)malloc(262144);
    if (!buf) return 0;
    int n = nk_slurp("/proc/self/maps", buf, 262143);
    if (n <= 0) { free(buf); return 0; }
    buf[n] = 0;
    for (int i = 0; i < (int)(sizeof g_bad_maps / sizeof g_bad_maps[0]); i++) {
        char *p = strstr(buf, g_bad_maps[i]);
        if (p) {
            /* 取出整行做日志 */
            char line[256];
            int k = 0;
            while (p > buf && p[-1] != '\n') p--;
            while (*p && *p != '\n' && k < (int)sizeof line - 1) line[k++] = *p++;
            line[k] = 0;
            nk_dbg("nk_ad[tool] maps hit %s | %s\n", g_bad_maps[i], line);
            free(buf);
            return 1;
        }
    }
    free(buf);
    return 0;
}

/* 逐个读 /proc/self/task 下每个 tid 的 comm（Frida 的 gum-js-loop / pool-frida 藏不住） */
static int nk_comm_hit(void) {
    char dir[64], buf[4096];
    int n = nk_slurp("/proc/self/task", buf, 4095);
    if (n <= 0) return 0;
    buf[n] = 0;
    char *p = buf;
    while (p && *p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        if (*p && strcmp(p, ".") && strcmp(p, "..")) {
            char path[96], name[64];
            snprintf(path, sizeof path, "/proc/self/task/%s/comm", p);
            int m = nk_slurp(path, name, 63);
            if (m > 0) {
                name[m] = 0;
                for (int i = 0; i < (int)(sizeof g_bad_comm / sizeof g_bad_comm[0]); i++)
                    if (strstr(name, g_bad_comm[i])) {
                        nk_dbg("nk_ad[tool] task comm=%s (tid %s)\n", name, p);
                        return 1;
                    }
            }
        }
        p = nl ? nl + 1 : NULL;
    }
    (void)dir;
    return 0;
}

/* Frida 默认端口 / D-Bus 抽象 socket */
static int nk_frida_socket(void) {
    static const char *ports[] = {"69A2", "69A3", "69AC"};   /* 27042 / 27043 / 27052 */
    static const char *netf[] = {"/proc/net/tcp", "/proc/net/tcp6"};
    char buf[16384];
    for (int f = 0; f < 2; f++) {
        int n = nk_slurp(netf[f], buf, 16383);
        if (n <= 0) continue;
        buf[n] = 0;
        char *p = buf;
        while ((p = strchr(p, ':')) != NULL) {
            /* local_address 形如 0100007F:69A2，冒号后面就是端口 */
            for (int i = 0; i < 3; i++) {
                if (strncmp(p + 1, ports[i], 4) == 0 && p[5] == ' ') {
                    nk_dbg("nk_ad[tool] %s listening :%s\n", netf[f], ports[i]);
                    return 1;
                }
            }
            p++;
        }
    }
    int nu = nk_slurp("/proc/net/unix", buf, 16383);
    if (nu > 0) {
        buf[nu] = 0;
        if (strstr(buf, "@frida") || strstr(buf, "frida-server") ||
            strstr(buf, "re.frida.server")) {
            nk_dbg("nk_ad[tool] unix socket frida\n");
            return 1;
        }
    }
    return 0;
}

/* 常见落盘位置（phantom-frida 会改名，这里只查默认名 + gadget） */
static int nk_frida_files(void) {
    static const char *f[] = {
        "/data/local/tmp/frida-server", "/data/local/tmp/re.frida.server",
        "/data/local/tmp/frida-gadget.so", "/data/local/tmp/libgadget.so",
        "/data/local/tmp/libfrida-gadget.so", "/data/local/tmp/hluda",
        "/data/adb/lspd", "/data/adb/zygisk_lsposed", "/data/adb/riru",
    };
    for (int i = 0; i < (int)(sizeof f / sizeof f[0]); i++)
        if (nk_sys(48, (long)f[i], 0, 0) == 0) {
            nk_dbg("nk_ad[tool] file %s\n", f[i]);
            return 1;
        }
    return 0;
}

/* ptrace/IDA：status 里 State: t（被跟踪停止）+ TracerPid + /proc/self/stat 第 3 域 */
static int nk_trace_state(void) {
    char st[4096];
    int n = nk_slurp("/proc/self/status", st, 4095);
    if (n > 0) {
        st[n] = 0;
        char *p = strstr(st, "State:");
        if (p && (p[7] == 't' || p[7] == 'T')) {
            nk_dbg("nk_ad[tool] status State: t\n");
            return 1;
        }
    }
    char s2[512];
    n = nk_slurp("/proc/self/stat", s2, 511);
    if (n > 0) {
        s2[n] = 0;
        char *p = strrchr(s2, ')');            /* comm 可能带空格，从右括号后找 */
        if (p && p[1] == ' ' && (p[2] == 't' || p[2] == 'T')) {
            nk_dbg("nk_ad[tool] stat state=t\n");
            return 1;
        }
    }
    return 0;
}

/* eBPF uprobe（edbg / stackplz）会在函数序言写 BRK；扫自己 .text 头 8KB */
static int nk_uprobe_brk(void) {
    if (!g_text_addr || g_text_size < 4096) return 0;
    const unsigned char *p = (const unsigned char *)g_text_addr;
    int hits = 0;
    for (int i = 0; i + 4 <= 8192 && i + 4 <= (int)g_text_size; i += 4) {
        unsigned w = (unsigned)p[i] | ((unsigned)p[i + 1] << 8) |
                     ((unsigned)p[i + 2] << 16) | ((unsigned)p[i + 3] << 24);
        if ((w & 0xFFE0001Fu) == 0xD4200000u) hits++;
    }
    if (hits > 0) {
        nk_dbg("nk_ad[tool] BRK in .text head: %d\n", hits);
        /* 我们自己的产物正常也为 0；>0 说明有 uprobe/断点被写进代码 */
        return 1;
    }
    return 0;
}

/* 硬件断点/单步的侧信道：连续两次读 cntvct 的执行间隔若出现异常尖峰，
 * 说明有 perf_event 断点在打（只记软分，避免误杀） */
static void nk_hwbp_hiccup(void) {
    unsigned long long frq = 0, a = 0, b = 0;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(frq));
    if (!frq) return;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(a));
    for (volatile int i = 0; i < 2000; i++) { }     /* ~几微秒的紧循环 */
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(b));
    double us = (double)(b - a) * 1e6 / (double)frq;
    nk_dbg("nk_ad[tool] tightloop=%.2fus\n", us);
    if (us > 2000.0) nk_soft("hwbp-hiccup");
}


/* v9：uprobe 痕迹 —— [uprobes] 条目 + 自身文本 VMA 可写（eDBG/eBPF 打桩前置条件） */
static int nk_uprobe_maps(void)
{
    char *buf = (char *)malloc(262144);
    if (!buf) return 0;
    int n = nk_slurp("/proc/self/maps", buf, 262143);
    if (n <= 0) { free(buf); return 0; }
    buf[n] = 0;
    if (strstr(buf, "[uprobes]")) {
        nk_dbg("nk_ad[tool] maps 里出现 [uprobes]\n");
        free(buf);
        return 1;
    }
    if (g_text_addr) {
        char *p = buf;
        while (p && *p) {
            unsigned long a = 0, b = 0;
            char perm[8] = {0};
            if (sscanf(p, "%lx-%lx %7s", &a, &b, perm) == 3 &&
                g_text_addr >= (uint64_t)a && g_text_addr < (uint64_t)b) {
                if (perm[1] == 'w') {
                    nk_dbg("nk_ad[tool] 自身文本可写: %s\n", perm);
                    free(buf);
                    return 1;
                }
                break;
            }
            p = strchr(p, '\n');
            if (p) p++;
        }
    }
    free(buf);
    return 0;
}

/* v9：主动阻断 —— 文本可写时内核会拒绝注册 uprobe。
 * Android 上 SELinux 大多直接拒绝 mprotect，成功与否都只记日志。 */
static void nk_text_wlock(void)
{
    if (!g_text_addr || !g_text_size) return;
    long pg = sysconf(_SC_PAGESIZE);
    if (pg <= 0) pg = 4096;
    uintptr_t base = (uintptr_t)g_text_addr & ~(uintptr_t)(pg - 1);
    uintptr_t end = ((uintptr_t)g_text_addr + g_text_size + (uintptr_t)pg - 1) & ~(uintptr_t)(pg - 1);
    size_t len = (size_t)(end - base);
    if (len > (size_t)(4u << 20)) len = (size_t)(4u << 20);
    int r = mprotect((void *)base, len, PROT_READ | PROT_WRITE | PROT_EXEC);
    nk_dbg("nk_ad[tool] mprotect 文本 RWX -> %d\n", r);
    if (r == 0) nk_soft("text-rwx-blocks-uprobe");
}

static void nk_tool_scan(int early) {
    if (nk_uprobe_maps())  nk_die_r("nk_ad: text writable / [uprobes]");
    if (nk_maps_hit())     nk_die_r("nk_ad: frida/xposed module in maps");
    if (nk_frida_socket()) nk_die_r("nk_ad: frida socket");
    if (nk_frida_files())  nk_die_r("nk_ad: injector artifacts");
    if (nk_comm_hit())     nk_die_r("nk_ad: injected thread name");
    if (nk_trace_state())  nk_die_r("nk_ad: traced state");
    if (!early) {
        if (nk_uprobe_brk()) nk_die_r("nk_ad: BRK patched into .text (uprobe)");
        nk_hwbp_hiccup();
        nk_text_wlock();
    }
}

static unsigned long long g_emu_last_deep;

static unsigned long long nk_now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000ULL + (unsigned long long)t.tv_nsec / 1000000ULL;
}

static void nk_emu_scan(int early) {
    if (!nk_emu_on()) return;

    if (nk_emu_prop())     nk_die_r("nk_ad: emulator build.prop");
    if (nk_emu_dev())      nk_die_r("nk_ad: emulator device node");
    if (nk_emu_cpu())      nk_die_r("nk_ad: emulator cpuinfo");
    if (nk_emu_auxv())     nk_die_r("nk_ad: emulated auxv (no vDSO)");
    if (nk_emu_procfs())   nk_die_r("nk_ad: /proc is not procfs");
    if (nk_emu_uname())    nk_die_r("nk_ad: uname mismatch");
    if (nk_emu_syscall())  nk_die_r("nk_ad: syscall semantics");

    if (!early) {
        /* 深度检测里有 20ms 的 sleep 和一次故意缺页，绝不能每次调用都跑 —— 4 秒一次 */
        unsigned long long now = nk_now_ms();
        if (now - g_emu_last_deep >= 4000ULL) {
            g_emu_last_deep = now;
            if (nk_emu_trap()) nk_die_r("nk_ad: segv si_addr");
            nk_emu_timer();
            nk_emu_name();
            nk_emu_task();
            nk_emu_maps();
            if (g_emu_score >= 3) nk_die_r("nk_ad: emulation score>=3");
        }
    }
}

static void nk_check_full(int early) {
    nk_dbg("nk_ad[chk] early=%d wd=%d\n", early, (int)g_wd_pid);

    nk_emu_scan(early);
    nk_tool_scan(early);

    if (nk_any_thread_traced()) nk_die_r("nk_ad: thread traced");

    char st[4096];
    if (nk_slurp("/proc/self/status", st, sizeof st) > 0) {
        char *t = strstr(st, "TracerPid:");
        if (t) {
            int pid = (int)strtol(t + 10, NULL, 10);
            nk_dbg("nk_ad[chk] TracerPid=%d wd=%d\n", pid, (int)g_wd_pid);
            if (pid != 0 && !nk_comm_is((pid_t)pid, "nk-wd"))
                nk_die_r("nk_ad: traced by foreign");
        }
    }

    // 注意：Vulkan / GL 驱动会在启动后 dlopen 出新的 r-x 映射，这是合法行为，
    // 不能当成 hook。真正强特征是可写可执行(rwx)映射。
    if (!early) {
        if (nk_has_rwx()) nk_die_r("nk_ad: rwx exec region");
        if (g_nmaps > 0 && nk_dbg_on()) {
            nk_map_t *cur = (nk_map_t *)malloc(sizeof(nk_map_t) * NK_MAXMAP);
            if (cur) {
                int n = nk_parse_maps(cur, NK_MAXMAP);
                int nw = 0;
                for (int i = 0; i < n; i++)
                    if (!nk_region_known(cur[i].lo, cur[i].hi)) nw++;
                nk_dbg("nk_ad[chk] maps +%d (仅日志)\n", nw);
                free(cur);
            }
        }
    }

    {
        int hk = nk_hooked_libc();
        nk_dbg("nk_ad[chk] hooked=%d\n", hk);
        if (hk) nk_die_r("nk_ad: libc prologue hooked");
    }

    if (!early && g_text_addr && g_text_size) {
        uint64_t h = nk_hash_mem((const unsigned char *)g_text_addr, g_text_size);
        nk_dbg("nk_ad[chk] text %llx vs %llx\n",
               (unsigned long long)h, (unsigned long long)g_text_hash);
        if (h != g_text_hash) nk_die_r("nk_ad: self text patched");
    }
}

__attribute__((visibility("default"))) void __nk_ad_check(void) {
#ifdef NK_AD_OFF
    return;   /* 关闭反调试：入口检查直接放行 */
#endif
    if (!g_inited) return;
    if ((++g_calls & 31u) != 0) return;
    nk_check_full(0);
}

static void *nk_thread(void *arg) {
    (void)arg;
    for (;;) {
        usleep(1500 * 1000);
        if (g_inited) nk_check_full(0);
    }
    return NULL;
}

static void nk_watchdog(pid_t target) {
    if (ptrace(PTRACE_SEIZE, target, 0, 0) != 0) _exit(0);
    for (;;) {
        int st = 0;
        if (waitpid(target, &st, WNOHANG) == target) _exit(0);
        if (kill(target, 0) != 0) _exit(0);
        usleep(300 * 1000);
    }
}

__attribute__((constructor(101))) static void nk_ad_init(void) {
    nk_dbg("nk_ad[init] enter dbg=%d\n", nk_dbg_on());
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);

    g_nmaps = nk_parse_maps(g_maps, NK_MAXMAP);
    if (g_nmaps < 0) g_nmaps = 0;

    struct nk_ctx ctx = {0, 0, 0};
    dl_iterate_phdr(nk_text_cb, &ctx);
    if (ctx.done) {
        g_text_addr = ctx.a;
        g_text_size = ctx.s;
        g_text_hash = nk_hash_mem((const unsigned char *)g_text_addr, g_text_size);
    }

    g_inited = 1;
    nk_check_full(1);

    pid_t self = getpid();
    pid_t wd = fork();
    if (wd == 0) {
        prctl(PR_SET_NAME, "nk-wd", 0, 0, 0);
        nk_watchdog(self);
        _exit(0);
    }
    if (wd > 0) {
        char st[4096];
        g_wd_pid = 0;
        for (int i = 0; i < 40; i++) {
            usleep(25 * 1000);
            if (nk_slurp("/proc/self/status", st, sizeof st) > 0) {
                char *t = strstr(st, "TracerPid:");
                if (t && (int)strtol(t + 10, NULL, 10) == (int)wd) { g_wd_pid = wd; break; }
            }
        }
        nk_dbg("nk_ad[init] watchdog=%d seized=%d\n", (int)wd, (int)g_wd_pid);
    }

    pthread_t th;
    if (pthread_create(&th, NULL, nk_thread, NULL) == 0) pthread_detach(th);
}
