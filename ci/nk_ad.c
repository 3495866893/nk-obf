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
    /* 单线程是原生程序最正常的状态，只记日志不进分数 */
    if (cnt <= 1) nk_soft_tool("single-thread");
    return 0;
}

/* ---- 12. maps 里必须有 [stack]；有 vDSO 就必须有 [vdso] ---- */
static int nk_emu_maps(void) {
    /* 缓冲必须够大：25 MB 二进制 + Vulkan/EGL/字体映射加起来轻易超过 16 KB，
     * 缓冲小了就永远读不到 [stack]/[vdso]，等于给真机白送两个假软分。 */
    const int CAP = 262144;
    char *buf = (char *)malloc(CAP);
    if (!buf) return 0;
    int n = nk_slurp("/proc/self/maps", buf, CAP - 1);
    if (n <= 0) { free(buf); return 0; }
    buf[n] = 0;
    int full = (n >= CAP - 1);
    int has_stack = (strstr(buf, "[stack]") != NULL);
    int has_vdso  = (strstr(buf, "[vdso]") != NULL);
    nk_dbg("nk_ad[emu] maps %d B full=%d stack=%d vdso=%d\n", n, full, has_stack, has_vdso);
    if (!full) {                      /* 只读到一部分就不下结论 */
        if (!has_stack) nk_soft("no-stack");
        if (!has_vdso)  nk_soft("no-vdso");
    }
    free(buf);
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
    /* Frida 17.x / phantom-frida 之后常见的运行时痕迹 */
    "gum-js", "frida:rpc", "libfrida-gadget", "frida_rpc", "gadget.so",
    "libgadget", "frida_helper",
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
/* 设备本身有没有 root 框架（KernelSU / Magisk / APatch）。有的话 /data/adb 下的
 * 目录属正常现象，只能记软分；没有却有，才是硬证据。 */
static int nk_root_framework(void)
{
    static const char *m[] = {
        "/data/adb/ksud", "/data/adb/ksu", "/data/adb/magisk",
        "/data/adb/apd", "/data/adb/ap", "/data/adb/modules",
    };
    for (int i = 0; i < (int)(sizeof m / sizeof m[0]); i++)
        if (nk_sys(48, (long)m[i], 0, 0) == 0) return 1;
    /* 框架被删但 /data/adb 仍在（升级/卸载残留）也算 root 环境，避免误杀。 */
    if (nk_sys(48, (long)"/data/adb", 0, 0) == 0) return 1;
    return 0;
}

static int nk_frida_files(void) {
    static const char *hard[] = {
        "/data/local/tmp/frida-server", "/data/local/tmp/re.frida.server",
        "/data/local/tmp/frida-gadget.so", "/data/local/tmp/libgadget.so",
        "/data/local/tmp/libfrida-gadget.so", "/data/local/tmp/hluda",
        "/data/local/tmp/frida", "/data/local/tmp/libfrida",
    };
    static const char *soft[] = {
        "/data/adb/lspd", "/data/adb/zygisk_lsposed", "/data/adb/riru",
        "/data/adb/modules/zygisk_lsposed", "/data/adb/modules/riru-core",
    };
    for (int i = 0; i < (int)(sizeof hard / sizeof hard[0]); i++)
        if (nk_sys(48, (long)hard[i], 0, 0) == 0) {
            nk_dbg("nk_ad[tool] file %s\n", hard[i]);
            return 1;
        }
    int rooted = nk_root_framework();
    for (int i = 0; i < (int)(sizeof soft / sizeof soft[0]); i++)
        if (nk_sys(48, (long)soft[i], 0, 0) == 0) {
            if (rooted) {
                nk_dbg("nk_ad[tool] %s（环境自带 root，记软分）\n", soft[i]);
                nk_soft_tool("xposed-dir-in-rooted");   /* P0：环境信息，不参与判死 */
            } else {
                nk_dbg("nk_ad[tool] file %s\n", soft[i]);
                return 1;
            }
        }
    return 0;
}

/* ptrace/IDA：status 里 State: t（被跟踪停止）+ TracerPid + /proc/self/stat 第 3 域 */
static int nk_trace_state(void)
{
    /* 开发期：NK_AD_ALLOW_DEBUG=1 放过 JDWP / Android Studio 附加，
     * 免得调试器一挂上就被判死 97。线上不要设这个变量。 */
    if (getenv("NK_AD_ALLOW_DEBUG")) return 0;
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
/* 硬件断点槽位自检：arm64 只有 4~6 个 DBGBVR/DBGBCR 槽。
 * 自己在自身文本上申请一个执行断点，perf_event_open 返回 ENOSPC/EBUSY
 * 说明槽位已被外部工具（universal-debug-tool / stackplz HWBP / ATrace）占满。
 * SELinux 常见的 EACCES/EPERM 一律放行（fail-open）。 */
static int nk_hwbp_slots(void)
{
    unsigned char at[128];
    memset(at, 0, sizeof at);
    uint32_t type = 5;                 /* PERF_TYPE_BREAKPOINT */
    uint32_t size = (uint32_t)sizeof at;
    memcpy(at + 0, &type, 4);
    memcpy(at + 4, &size, 4);
    uint64_t flags = (1ull << 5) | (1ull << 6);   /* exclude_kernel | exclude_hv */
    memcpy(at + 40, &flags, 8);
    uint32_t bp_type = 4;              /* HW_BREAKPOINT_X */
    memcpy(at + 52, &bp_type, 4);
    uint64_t addr = g_text_addr ? g_text_addr
                                : (uint64_t)(uintptr_t)&nk_hwbp_slots;
    memcpy(at + 56, &addr, 8);
    uint64_t len = 4;                  /* HW_BREAKPOINT_LEN_4 */
    memcpy(at + 64, &len, 8);

    long fd = nk_sys5(241, (long)(uintptr_t)at, 0, -1, -1, 0);
    if (fd >= 0) {
        nk_sys(57, fd, 0, 0);          /* close */
        nk_dbg("nk_ad[tool] hwbp slot free\n");
        return 0;
    }
    /* P1：nk_sys5 是内联 svc #0，不经过 libc，失败返回 -errno（errno 恒为 0） */
    long e = -fd;
    nk_dbg("nk_ad[tool] perf_event_open -> %ld errno %ld\n", fd, e);
    if (e == 28 /*ENOSPC*/ || e == 16 /*EBUSY*/) return 1;
    return 0;
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
                if (perm[1] == 'w' && !g_text_rwx_self) {
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
    /* P1.5：默认不做。把 .text 改成 RWX 能挡内核在此装 uprobe（eDBG/stackplz 失效），
     * 但同时等于替 inline hook 把门打开，对 Frida 是净负收益 —— 要防 eDBG 才显式开：
     *   NK_AD_WLOCK=1
     * 另外无论成败都只尝试一次，避免每轮白跑一次 mprotect。 */
    if (g_wlock_tried) return;
    g_wlock_tried = 1;
    const char *wl = getenv("NK_AD_WLOCK");
    if (!wl || wl[0] != '1') return;
    long pg = sysconf(_SC_PAGESIZE);
    if (pg <= 0) pg = 4096;
    uintptr_t base = (uintptr_t)g_text_addr & ~(uintptr_t)(pg - 1);
    uintptr_t end = ((uintptr_t)g_text_addr + g_text_size + (uintptr_t)pg - 1) & ~(uintptr_t)(pg - 1);
    size_t len = (size_t)(end - base);
    if (len > (size_t)(4u << 20)) len = (size_t)(4u << 20);
    int r = mprotect((void *)base, len, PROT_READ | PROT_WRITE | PROT_EXEC);
    if (r == 0) g_text_rwx_self = 1;
    nk_dbg("nk_ad[tool] mprotect 文本 RWX -> %d\n", r);
    if (r == 0) nk_soft_tool("text-rwx-blocks-uprobe");   /* 这是自己的行为，不算证据 */
}


/* ================= v10：补齐 mj(3).md 的八条 ================= */

/* 3) 只带 svc #0 的 getpid，用来验证系统调用通路没被拦 */
static long nk_raw_getpid(void)
{
    register long x8 __asm__("x8") = 172;      /* getpid */
    register long x0 __asm__("x0");
    __asm__ __volatile__("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}

/* v11.2 P2：单个 getpid 只能抓「完全不转发」的 hook；
 * 加 getppid / gettid 交叉验证，QTrace 这类「记录 + 转发」的包装会露馅。 */
static long nk_raw_sys(long nr)
{
    register long x8 __asm__("x8") = nr;
    register long x0 __asm__("x0");
    __asm__ __volatile__("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}

static int nk_svc_ok(void)
{
    if (nk_raw_getpid() != (long)getpid()) return 0;
    if (nk_raw_sys(173) != (long)getppid()) return 0;   /* getppid */
    if (nk_raw_sys(178) != syscall(178)) return 0;      /* gettid（API21 无 gettid() 声明） */
    return 1;
}

/* 2)/7) 匿名可执行映射 / memfd 加载：Frida gadget、linjector 落地的典型形态。
 * 纯 native 程序不该有「没有任何路径」的可执行段。 */
static int nk_anon_exec(void)
{
    char *buf = (char *)malloc(262144);
    if (!buf) return 0;
    int n = nk_slurp("/proc/self/maps", buf, 262143);
    if (n <= 0) { free(buf); return 0; }
    buf[n] = 0;
    int hit = 0;
    char *p = buf;
    while (p && *p) {
        char *nl = strchr(p, '\n');
        unsigned long a = 0, b = 0;
        char perm[8] = {0};
        int nfield = 0;
        if (sscanf(p, "%lx-%lx %7s %*s %*s %*s %n", &a, &b, perm, &nfield) >= 3 &&
            perm[2] == 'x') {
            /* maps 每行：addr perms offset dev inode pathname，字段之间可能有多个空格；
             * 用 %n 取「inode 之后」的偏移，再手动跳过空白。 */
            char *q = p + (nfield > 0 ? nfield : 0);
            while (*q == ' ' || *q == '\t') q++;
            int self = g_text_addr && g_text_addr >= (uint64_t)a &&
                       g_text_addr < (uint64_t)b;
            if (!self && q) {
                if (*q == '/') {                        /* 文件映射，正常 */
                    /* 继续 */
                } else if (strncmp(q, "memfd:", 6) == 0) {
                    /* P0：ART 的 JIT code cache 本身就是 memfd，名字带 jit：
                     *   /memfd:jit-cache、/memfd:jit-cache-zygote、/memfd:jit-zygote-cache
                     * 现代 App 启动几十毫秒内就出现，硬杀等于把自己杀掉。 */
                    /* 只认 ART 的真实命名，且必须是 "memfd:" 之后的前缀。
                     * 以前用 strstr(q,"jit") 任意位置匹配，
                     * 攻击者把 gadget 命名成 /memfd:frida-jit-agent 就绕过了。 */
                    const char *mname = q + 6;
                    if (strncmp(mname, "jit-cache", 9) == 0 ||
                        strncmp(mname, "jit-zygote", 10) == 0 ||
                        strncmp(mname, "jit_zygote", 10) == 0 ||
                        strncmp(mname, "jit_cache", 9) == 0) {
                        nk_dbg("nk_ad[tool] memfd exec 合法 JIT，放过: %s\n", q);
                    } else {
                        nk_dbg("nk_ad[tool] memfd exec %s\n", q);
                        hit = 1;
                    }
                } else if (*q == '\n' || *q == 0) {      /* 完全没有路径 */
                    nk_dbg("nk_ad[tool] pathless exec %lx-%lx %s\n", a, b, perm);
                    nk_soft_tool("pathless-exec");      /* P1：GPU/JIT 变体可能合法，只日志 */
                }
            }
        }
        p = nl ? nl + 1 : NULL;
    }
    free(buf);
    return hit;
}

/* 1)/8) JNI 派发表完整性：JavaVM / JNIEnv 的函数指针必须都落在 ART 模块内。
 * 被 LSPosed/Xposed 或 ART 层 hook 过时，表项会指向外来内存。 */
static int nk_jni_table_ok(void)
{
    typedef long (*getvms_t)(void **, long, long *);
    getvms_t g = (getvms_t)dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs");
    if (!g) return -1;                              /* 不是 ART 进程，跳过 */
    void *vm = NULL;
    long nvm = 0;
    if (g(&vm, 1, &nvm) != 0 || !vm || nvm < 1) return -1;
    void **vt = *(void ***)vm;
    if (!vt) return 1;
    Dl_info di;
    if (dladdr((void *)vt, &di) == 0 || !di.dli_fbase) return 0;
    uint64_t lo = (uint64_t)di.dli_fbase;
    uint64_t hi = lo + (64u << 20);
    for (int i = 0; i < 8; i++) {
        void *fn = vt[i];
        if (!fn) continue;
        Dl_info d2;
        if (dladdr(fn, &d2) == 0 || !d2.dli_fbase) {
            nk_dbg("nk_ad[tool] JNI vtable[%d]=%p 不在任何模块内\n", i, fn);
            return 0;
        }
        uint64_t base = (uint64_t)d2.dli_fbase;
        if (base < lo || base >= hi) {
            nk_dbg("nk_ad[tool] JNI vtable[%d] 跳到模块外 %p\n", i, fn);
            return 0;
        }
        if (nk_hooked_at_in((const unsigned char *)fn, base, base + (16u << 20)))
            return 0;
    }
    return 1;
}

/* 6) 关键模块的代码段哈希基线（主模块 + libc/libart/libc++ 等） */
typedef struct { uint64_t a, s, h; } nk_tm_t;
static nk_tm_t g_tm[8];
static int     g_ntm = 0;

static int nk_tm_cb(struct dl_phdr_info *info, size_t sz, void *data)
{
    (void)sz; (void)data;
    /* 基线现在延后到「首次深检」时采集，那时 g_inited 已是 1，
     * 所以这里只按容量截断，不能再判 g_inited。 */
    if (g_ntm >= 8) return 1;
    const char *nm = info->dlpi_name ? info->dlpi_name : "";
    /* P0：只哈希「运行时不会自己改自己」的模块。
     * libart.so 会被 ART 合法写入（JIT trampoline 回填、deopt 改入口、
     * CPU 特性 patch、profile 引导重编译），全段哈希必然误杀；
     * libc++.so / libnativehelper 同理（运行时补丁 + 内联 thunk）。 */
    int keep = (nm[0] == 0) || strstr(nm, "libc.so") ||
               strstr(nm, "libm.so") || strstr(nm, "libdl.so");
    if (!keep) return 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type == PT_LOAD && (ph->p_flags & PF_X)) {
            uint64_t a = (uint64_t)info->dlpi_addr + ph->p_vaddr;
            uint64_t s = ph->p_memsz > (256u << 10) ? (uint64_t)(256u << 10)
                                                    : (uint64_t)ph->p_memsz;
            g_tm[g_ntm].a = a;
            g_tm[g_ntm].s = s;
            g_tm[g_ntm].h = nk_hash_mem((const unsigned char *)a, s);
            g_ntm++;
            break;
        }
    }
    return 0;
}

/* 0=未采  1=采集中  2=就绪；用原子交换防并发重入。 */
static int g_tm_ready = 0;

static void nk_tm_collect(void)
{
    g_ntm = 0;
    dl_iterate_phdr(nk_tm_cb, NULL);
    nk_dbg("nk_ad[tool] 代码段基线 %d 个模块\n", g_ntm);
}

static void nk_tm_verify(void)
{
    /* P0：基线延后到首次深检采集，避开 ART 启动期的合法自修改。
     * v11.2：采完不再 return，当轮立刻 verify 一次（把漏检窗口从 1.5s 收到 0）。 */
    int first = 0;
    if (__atomic_load_n(&g_tm_ready, __ATOMIC_ACQUIRE) == 0 &&
        __atomic_exchange_n(&g_tm_ready, 1, __ATOMIC_ACQ_REL) == 0) {
        nk_tm_collect();
        __atomic_store_n(&g_tm_ready, 2, __ATOMIC_RELEASE);
        first = 1;
        nk_dbg("nk_ad[tool] 基线延后采样完成，立即校验一次\n");
    }
    if (!first && __atomic_load_n(&g_tm_ready, __ATOMIC_ACQUIRE) != 2) return;
    for (int i = 0; i < g_ntm; i++) {
        uint64_t h = nk_hash_mem((const unsigned char *)g_tm[i].a, g_tm[i].s);
        if (h != g_tm[i].h) {
            nk_dbg("nk_ad[tool] 模块 %d 代码段被改 %llx vs %llx\n", i,
                   (unsigned long long)h, (unsigned long long)g_tm[i].h);
            nk_die_r("nk_ad: critical module .text patched");
            return;
        }
    }
}

static int nk_addr_exec(uint64_t a);   /* v11.5 定义，v11.6 深检里要用 */

/* ================= v11.6 IR 级 ART 表校验（Pass 直接读写这三个符号） ================= */

/* 为什么放在这里：Pass 会在**每个函数入口**生成一段 IR，
 * 直接读这三个全局量，把 JavaVM 的函数表逐项与 [lo, hi] 比较，
 * 任何一项越界就 call __nk_ad_panic()。这样校验点被混淆进调用方，
 * 不像纯 C 函数那样容易被单点 nop 掉。 */
unsigned long __nk_art_lo = 0;
unsigned long __nk_art_hi = 0;
void *volatile __nk_vm = 0;

void __nk_ad_panic(void)
{
    nk_die_r("nk_ad: art jni table corrupted");
}

/* 扫 maps 求 libart.so 的加载区间（各段取 min lo / max hi），并拿到 JavaVM* */
static void nk_art_bind(void)
{
    char *buf = (char *)malloc(262144);
    if (buf) {
        int n = nk_slurp("/proc/self/maps", buf, 262143);
        if (n > 0) {
            buf[n] = 0;
            unsigned long lo = ~0UL, hi = 0;
            char *p = buf;
            while (p && *p) {
                char *nl = strchr(p, '\n');
                if (nl) *nl = 0;
                if (strstr(p, "libart.so")) {
                    unsigned long a = 0, b = 0;
                    if (sscanf(p, "%lx-%lx", &a, &b) == 2) {
                        if (a < lo) lo = a;
                        if (b > hi) hi = b;
                    }
                }
                if (!nl) break;
                p = nl + 1;
            }
            if (hi > lo) { __nk_art_lo = lo; __nk_art_hi = hi; }
        }
        free(buf);
    }
    if (!__nk_vm) {
        typedef long (*getvms_t)(void **, long, long *);
        getvms_t g = (getvms_t)dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs");
        if (g) {
            void *vm = NULL;
            long nvm = 0;
            if (g(&vm, 1, &nvm) == 0 && vm && nvm >= 1) __nk_vm = vm;
        }
    }
    nk_dbg("nk_ad[art] lo=%lx hi=%lx vm=%p\n", __nk_art_lo, __nk_art_hi, (void *)__nk_vm);
}

/* C 侧深检：JNIEnv 表前 64 项 + jmethodID 里的 ArtMethod 入口点 */
void __nk_art_check(int deep)
{
    if (!__nk_art_lo || !__nk_vm) nk_art_bind();
    if (!__nk_art_lo || !__nk_art_hi || !__nk_vm) return;      /* 非 ART 进程，放过 */
    unsigned long lo = __nk_art_lo, hi = __nk_art_hi;

    void **vt = *(void ***)__nk_vm;
    if (!vt) return;
    for (int i = 0; i < 8; i++) {                              /* JNIInvokeInterface 共 8 项 */
        unsigned long f = (unsigned long)(uintptr_t)vt[i];
        if (f < lo || f > hi) {
            nk_dbg("nk_ad[art] JavaVM.vtable[%d]=%lx 越界 [%lx,%lx]\n", i, f, lo, hi);
            __nk_ad_panic();
        }
    }
    if (!deep) return;

    typedef long (*getenv_t)(void *, void **, long);
    getenv_t ge = (getenv_t)vt[6];                             /* GetEnv（索引 6） */
    if (!ge) return;
    void *env = NULL;
    if (ge(__nk_vm, &env, 0x00010006L) != 0 || !env) return;
    void **evt = *(void ***)env;
    if (!evt) return;
    for (int i = 0; i < 64; i++) {                             /* JNINativeInterface 头部 */
        unsigned long f = (unsigned long)(uintptr_t)evt[i];
        if (f < lo || f > hi) {
            nk_dbg("nk_ad[art] JNIEnv.vtable[%d]=%lx 越界\n", i, f);
            __nk_ad_panic();
        }
    }

    /* ArtMethod 入口点：jmethodID 在 ART 里就是 ArtMethod*，
     * 取 24/32/40 字节处的入口指针，必须落在可执行映射或 libart 内。 */
    typedef void *(*findclass_t)(void *, const char *);
    typedef void *(*getmid_t)(void *, void *, const char *, const char *);
    findclass_t fc = (findclass_t)evt[6];                      /* FindClass */
    getmid_t   gm = (getmid_t)evt[33];                         /* GetMethodID */
    if (!fc || !gm) return;
    void *cls = fc(env, "java/lang/Object");
    if (!cls) return;
    void *mid = gm(env, cls, "hashCode", "()I");
    if (!mid) return;
    /* 只查 offset 24：entry_point_from_quick_compiled_code_。
     * 其它槽位在各 Android 版本语义不同（data_ 可能是指针也可能是指令流偏移），
     * 一律不当成执行地址，否则只会污染日志。 */
    uint64_t *w = (uint64_t *)mid;
    {
        uint64_t v = w[3];
        uint64_t a = v & ~7ULL;
        if (a >= 0x10000ULL) {                                 /* 0 = 还没编译，正常 */
            Dl_info di;
            int ok = (dladdr((void *)a, &di) && di.dli_fbase) || nk_addr_exec(a);
            if (!ok) {
                nk_dbg("nk_ad[art] ArtMethod.entry=%p 不在可执行区\n", (void *)a);
                __nk_ad_panic();
            }
        }
    }
}

/* ================= v11.5 行为验证：不匹配指纹，验证「世界是否正常」 ================= */

/* 某地址是否落在可执行映射里（现查 maps，稳妥，不依赖缓存） */
static int nk_addr_exec(uint64_t a)
{
    char *buf = (char *)malloc(262144);
    if (!buf) return 1;                       /* fail-open：查不了就当正常 */
    int n = nk_slurp("/proc/self/maps", buf, 262143);
    int ok = 0;
    if (n > 0) {
        buf[n] = 0;
        char *p = buf;
        while (p && *p) {
            char *nl = strchr(p, '\n');
            if (nl) *nl = 0;
            unsigned long long lo = 0, hi = 0;
            char perm[8] = {0};
            if (sscanf(p, "%llx-%llx %7s", &lo, &hi, perm) == 3) {
                if (a >= lo && a < hi) { ok = (perm[2] == 'x'); break; }
            }
            if (!nl) break;
            p = nl + 1;
        }
    }
    free(buf);
    return ok;
}

/* A) ArtMethod 入口点：ART 里 jmethodID 就是 ArtMethod*，
 *    取几个指针槽位，看解引用出来的入口是否落在可执行映射。
 *    artseek / 部分 ART hook 会把入口改到堆或匿名页上。 */
static void nk_art_entry_check(void)
{
    typedef long (*getvms_t)(void **, long, long *);
    getvms_t gv = (getvms_t)dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs");
    if (!gv) return;
    void *vm = NULL;
    long nvm = 0;
    if (gv(&vm, 1, &nvm) != 0 || !vm || nvm < 1) return;
    void **vt = *(void ***)vm;
    if (!vt) return;
    typedef long (*getenv_t)(void *, void **, long);
    getenv_t ge = (getenv_t)vt[6];             /* JNIInvokeInterface::GetEnv（索引 6） */
    if (!ge) return;
    void *env = NULL;
    if (ge(vm, &env, 0x00010006L) != 0 || !env) return;   /* 线程没附着 → 放过 */
    void **evt = *(void ***)env;
    if (!evt) return;
    typedef void *(*findclass_t)(void *, const char *);
    typedef void *(*getmid_t)(void *, void *, const char *, const char *);
    findclass_t fc = (findclass_t)evt[6];      /* JNINativeInterface::FindClass */
    getmid_t   gm = (getmid_t)evt[33];         /* JNINativeInterface::GetMethodID */
    if (!fc || !gm) return;
    void *cls = fc(env, "java/lang/Object");
    if (!cls) return;
    void *mid = gm(env, cls, "hashCode", "()I");
    if (!mid) return;

    uint64_t *w = (uint64_t *)mid;
    for (int i = 3; i <= 5; i++) {             /* 24 / 32 / 40 字节处的入口槽 */
        uint64_t v = w[i];
        uint64_t a = v & ~7ULL;                /* 低 3 位可能是 tag */
        if (a < 0x10000ULL) continue;          /* 0 或小值 = 还没编译，正常 */
        if (nk_addr_exec(a)) continue;
        Dl_info di;
        if (dladdr((void *)a, &di) && di.dli_fbase) continue;
        nk_dbg("nk_ad[tool] ArtMethod.word[%d]=%p 落在非可执行区\n", i, (void *)a);
        nk_soft_tool("art-entry-outside-exec");
        return;
    }
}

/* B) 缓存侧信道：32 MB 随机步进，真机 L1 命中与 DRAM 未命中差 3~10 倍；
 *    QBDI / unicornTrace 这类软件模拟每次访问耗时几乎一样（比值接近 1）。 */
static unsigned long long nk_cntvct(void)
{
    unsigned long long v;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}

static void nk_cache_probe(void)
{
    const size_t SZ = 8u << 20;                  /* 8 MB：超过 L2/L3 就足够制造 DRAM 访问 */
    unsigned char *p = (unsigned char *)mmap(NULL, SZ, PROT_READ | PROT_WRITE,
                                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return;
    for (size_t i = 0; i < SZ; i += 4096) p[i] = 1;          /* 先把页提交 */
    unsigned long long mn = ~0ULL, mx = 0, sum = 0;
    unsigned seed = 12345u;
    const int N = 1024;
    for (int i = 0; i < N; i++) {
        seed = seed * 1103515245u + 12345u;
        size_t off = ((size_t)((seed >> 8) % (unsigned)(SZ / 64))) * 64;
        unsigned long long t0 = nk_cntvct();
        volatile unsigned char sink = p[off];
        unsigned long long t1 = nk_cntvct();
        (void)sink;
        unsigned long long d = t1 - t0;
        if (d < mn) mn = d;
        if (d > mx) mx = d;
        sum += d;
    }
    munmap(p, SZ);
    if (mn == 0 || sum == 0) return;
    unsigned long long avg = sum / (unsigned long long)N;
    unsigned long long r10 = mx * 10ULL / mn;
    nk_dbg("nk_ad[tool] cache min=%llu avg=%llu max=%llu ratio=%llu.%llu\n",
           mn, avg, mx, r10 / 10ULL, r10 % 10ULL);
    if (r10 < 13ULL)      nk_soft_tool("cache-latency-flat");
    else if (r10 < 20ULL) nk_soft_tool("cache-latency-uniform");
}

/* C) 硬件断点主动触发：给自己的 canary 下执行断点，调用 64 次后看计数器。
 *    计数器不涨 = 硬件断点机制被底层劫持（或内核把事件吃了）。 */
static volatile int g_canary_sink;

__attribute__((noinline)) static int nk_canary(int x)
{
    volatile int t = x * 3 + 1;
    return t ^ 0x5a5a;
}

static void nk_hwbp_trigger(void)
{
    /* perf_event_open 在 untrusted_app 域通常被 SELinux 拒（EACCES）；
     * 没 root 框架就别白花一次 syscall。有 root 时才可能真的拿到断点槽。 */
    if (!nk_root_framework()) return;
    unsigned char attr[128];
    memset(attr, 0, sizeof attr);
    *(uint32_t *)(attr + 0)  = 5;                            /* PERF_TYPE_BREAKPOINT */
    *(uint32_t *)(attr + 4)  = (uint32_t)sizeof attr;
    *(uint64_t *)(attr + 40) = (1ULL << 5) | (1ULL << 6);    /* exclude_kernel|hv */
    *(uint32_t *)(attr + 52) = 4;                            /* HW_BREAKPOINT_X */
    *(uint64_t *)(attr + 56) = (uint64_t)(uintptr_t)&nk_canary;
    *(uint64_t *)(attr + 64) = 4;                            /* len */
    long fd = nk_sys5(241, (long)attr, 0, -1, -1, 0);
    if (fd < 0) return;                                      /* 无权限/被占，交给别的检查 */
    unsigned long long c0 = 0, c1 = 0;
    if (nk_sys(63, fd, (long)&c0, 8) != 8) { nk_sys(57, fd, 0, 0); return; }
    int sink = 0;
    for (int i = 0; i < 64; i++) sink += nk_canary(i);
    long r = nk_sys(63, fd, (long)&c1, 8);
    nk_sys(57, fd, 0, 0);
    g_canary_sink = sink;
    if (r != 8) return;
    nk_dbg("nk_ad[tool] hwbp counter %llu -> %llu\n", c0, c1);
    if (c1 == c0) nk_soft_tool("hwbp-not-counting");
}

/* D) 时间源一致性：cntvct 推算的纳秒 vs CLOCK_MONOTONIC，偏离 30% 以上记软分 */
static void nk_time_probe(void)
{
    unsigned long long f = 0;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(f));
    if (!f) return;
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    unsigned long long t0 = nk_cntvct();
    volatile unsigned long long acc = 0;
    for (int i = 0; i < 200000; i++) {
        __asm__ __volatile__("" ::: "memory");   /* 阻止指令重排，保证计时对象稳定 */
        acc += (unsigned long long)i;
    }
    (void)acc;
    unsigned long long t1 = nk_cntvct();
    clock_gettime(CLOCK_MONOTONIC, &b);
    double ns = (double)(b.tv_sec - a.tv_sec) * 1e9 + (double)(b.tv_nsec - a.tv_nsec);
    if (ns < 1000.0) return;
    double expect = (double)(t1 - t0) / (double)f * 1e9;
    if (expect <= 0.0) return;
    double ratio = expect / ns;
    nk_dbg("nk_ad[tool] time expect=%.0fns real=%.0fns ratio=%.2f\n", expect, ns, ratio);
    if (ratio < 0.70 || ratio > 1.40) nk_soft_tool("time-desync");
}

/* 深度行为检查：60 秒一次，全部 fail-open */
static unsigned long long g_beh_last;
static unsigned long long g_beh_boot;

/* v12：行为检查里用到的两个低频判据，定义在后面，这里先声明 */
static void nk_thread_anomaly(void);
static void nk_hwbp_slot_count(void);

static void nk_behavior_scan(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    unsigned long long now = (unsigned long long)ts.tv_sec * 1000ULL +
                             (unsigned long long)ts.tv_nsec / 1000000ULL;
    /* 启动后 30 秒内不做行为侧信道：首轮深检发生在 1.5 s，
     * 那时候连内存都还没热起来，测出来的延迟曲线没意义。 */
    if (g_beh_boot == 0) { g_beh_boot = now; return; }
    if (now - g_beh_boot < 30000ULL) return;
    if (g_beh_last && now - g_beh_last < 60000ULL) return;
    g_beh_last = now;
    if (!__nk_vm || !__nk_art_lo) nk_art_bind();
    __nk_art_check(1);
    nk_art_entry_check();
    nk_hwbp_trigger();
    nk_time_probe();
    nk_cache_probe();
    /* v12：低频项（线程枚举 + 槽位枚举）只在行为检查里跑，避免 1.5 s 一轮的开销 */
    nk_thread_anomaly();
    nk_hwbp_slot_count();
}


/* =====================================================================
 * v12 加固块（P-2/P-3/P-4/P-5/P-6 + S 系列）
 * 全部新判据先软分；只有「已确立的硬证据」才 nk_die_r。
 * ===================================================================== */

/* ---------- P-2 看门狗互锁：存活 / 身份 / 僵尸回收 ---------- */
static unsigned long long nk_proc_starttime(pid_t p) {
    char path[64], buf[1024];
    snprintf(path, sizeof path, "/proc/%d/stat", (int)p);
    if (nk_slurp(path, buf, sizeof buf) <= 0) return 0;
    char *q = strrchr(buf, ')');                 /* comm 里可能有空格 */
    if (!q) return 0;
    int f = 0; char *sp = NULL;
    for (char *t = q + 1; *t; t++)
        if (*t == ' ') { sp = t; if (++f == 20) break; }
    return sp ? strtoull(sp + 1, NULL, 10) : 0;  /* 第 22 域 starttime */
}

static unsigned long long g_wd_start = 0;

/* 主进程侧：看门狗死了 / pid 被回收复用 -> 软分并放弃它（不判死，防误杀） */
static void nk_wd_reap(void) {
    if (g_wd_pid <= 0) return;
    int st = 0;
    pid_t r = waitpid(g_wd_pid, &st, WNOHANG);
    if (r == g_wd_pid) { nk_soft_tool("wd-exited"); g_wd_pid = -1; return; }
    if (kill(g_wd_pid, 0) != 0) { nk_soft_tool("wd-gone"); g_wd_pid = -1; }
}

/* 交叉验证：看门狗的身份不能只看 pid，pid 会被回收复用，用 starttime 兜底 */
static void nk_peer_verify(void) {
    if (g_wd_pid <= 0) return;
    unsigned long long st = nk_proc_starttime(g_wd_pid);
    if (st == 0) return;                          /* 读不到 -> 放过 */
    if (g_wd_start == 0) { g_wd_start = st; return; }
    if (st != g_wd_start) { nk_soft_tool("wd-pid-recycled"); g_wd_pid = -1; }
}

/* ---------- P-6 maps 单读单解析（新判据统一消费这一份） ---------- */
typedef struct {
    uint64_t lo, hi;
    char perm[5];
    unsigned char pathless;
    char path[192];
} nk_vma_t;

#define NK_VMA_MAX 320
static nk_vma_t *g_vma = NULL;
static int g_vma_n = 0;
static unsigned long long g_vma_gen = 0;
static unsigned long long g_vma_gen_used = 0;

static int nk_vma_refresh(int force) {
    if (!force && g_vma && g_vma_gen == g_vma_gen_used) return g_vma_n;
    if (!g_vma) {
        g_vma = (nk_vma_t *)malloc(sizeof(nk_vma_t) * NK_VMA_MAX);
        if (!g_vma) return 0;
    }
    char *buf = (char *)malloc(1024 * 1024);
    if (!buf) return g_vma_n;
    int n = nk_slurp("/proc/self/maps", buf, 1024 * 1024);
    if (n <= 0) { free(buf); return g_vma_n; }     /* 读不到 -> 用旧数据 */
    int cnt = 0; char *p = buf;
    while (p && *p && cnt < NK_VMA_MAX) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        unsigned long lo = 0, hi = 0;
        char perm[8] = {0};
        int off = 0;
        if (sscanf(p, "%lx-%lx %7s %*s %*s %*s %n", &lo, &hi, perm, &off) >= 3) {
            nk_vma_t *v = &g_vma[cnt];
            v->lo = lo; v->hi = hi;
            v->perm[0] = perm[0]; v->perm[1] = perm[1];
            v->perm[2] = perm[2]; v->perm[3] = perm[3]; v->perm[4] = 0;
            const char *path = (off > 0 && p[off]) ? p + off : "";
            v->pathless = (*path == 0) ? 1 : 0;
            snprintf(v->path, sizeof v->path, "%s", path);
            cnt++;
        }
        p = nl ? nl + 1 : NULL;
    }
    free(buf);
    g_vma_n = cnt;
    g_vma_gen++;
    g_vma_gen_used = g_vma_gen;
    return cnt;
}

/* ---------- P-4 结构化判据：路径合法性 / 已删除可执行 / memfd ---------- */
static int nk_path_legit(const char *p) {
    static const char *ok[] = {
        "/system/", "/system_ext/", "/vendor/", "/apex/", "/product/", "/odm/",
        "/data/app/", "/data/data/", "/data/user/", "/data/dalvik-cache/",
        "/data/local/", "/mnt/", "[", NULL
    };
    if (!p || !*p) return 1;
    if (p[0] != '/') return 1;                     /* [anon] / [stack] 等 */
    for (int i = 0; ok[i]; i++)
        if (!strncmp(p, ok[i], strlen(ok[i]))) return 1;
    return 0;
}

static void nk_struct_scan(void) {
    int n = nk_vma_refresh(0);
    for (int i = 0; i < n; i++) {
        if (g_vma[i].perm[2] != 'x') continue;
        const char *path = g_vma[i].path;
        if (!path[0]) { nk_soft_tool("exec-pathless"); continue; }
        if (strstr(path, " (deleted)")) { nk_soft_tool("exec-deleted"); continue; }
        if (!strncmp(path, "/memfd:", 7)) {
            const char *q = path + 7;
            /* ART JIT 的 memfd 是合法的，只认前缀 */
            if (strncmp(q, "jit-cache", 9) && strncmp(q, "jit-zygote", 10) &&
                strncmp(q, "jit_zygote", 10) && strncmp(q, "jit_cache", 9))
                nk_soft_tool("exec-memfd");
            continue;
        }
        if (!nk_path_legit(path)) nk_soft_tool("exec-suspicious-path");
    }
}

/* 线程检测改成「阈值 + 白名单外计数」，改名绕不过去 */
static void nk_thread_anomaly(void) {
    int total = 0, unknown = 0;
    char path[64], b[64];
    for (int tid = 1; tid < 2048 && total < 200; tid++) {
        snprintf(path, sizeof path, "/proc/self/task/%d/comm", tid);
        if (nk_slurp(path, b, sizeof b) <= 0) continue;
        total++;
        if (!strncmp(b, "nk-", 3)) continue;
        if (strstr(b, "binder") || strstr(b, "RenderThread") || strstr(b, "Chrome_") ||
            strstr(b, "Jit") || strstr(b, "Heap") || strstr(b, "Finalizer") ||
            strstr(b, "Signal") || strstr(b, "Timer") || strstr(b, "OkHttp") ||
            strstr(b, "RxCached") || strstr(b, "pool") || strstr(b, "worker") ||
            strstr(b, "main") || strstr(b, "Compiler") || strstr(b, "Reference"))
            continue;
        unknown++;
    }
    if (total == 0) return;
    if (unknown > 12) nk_soft_tool("thread-name-anomaly");
}

/* ---------- P-5 交叉验证：裸 svc 的 ppid 与 /proc 口径必须一致 ---------- */
static void nk_cross_conflict(void) {
    long rp = nk_raw_sys(173);                     /* getppid 裸 svc */
    if (rp < 0) return;
    char st[2048];
    if (nk_slurp("/proc/self/status", st, sizeof st) <= 0) return;
    char *t = strstr(st, "PPid:");
    if (!t) return;
    long pp = strtol(t + 5, NULL, 10);
    if (pp >= 0 && rp != pp) {
        nk_dbg("nk_ad[chk] ppid raw=%ld proc=%ld\n", rp, pp);
        nk_soft_tool("ppid-cross-mismatch");
    }
}

/* ---------- P-3 分段自哈希：轮转覆盖全量 .text，成本摊平 ---------- */
#define NK_SEG 8
static uint64_t g_seg_base[NK_SEG];
static int      g_seg_have[NK_SEG];
static int      g_seg_cur = 0;

static void nk_seg_verify(void) {
    if (!g_text_addr || !g_text_size) return;
    uint64_t per = g_text_size / NK_SEG;
    if (per < 4096) return;
    int i = g_seg_cur;
    g_seg_cur = (g_seg_cur + 1) % NK_SEG;
    uint64_t off = (uint64_t)i * per;
    uint64_t len = (i == NK_SEG - 1) ? (g_text_size - off) : per;
    uint64_t h = nk_hash_mem((const unsigned char *)(g_text_addr + off), len);
    if (!g_seg_have[i]) { g_seg_base[i] = h; g_seg_have[i] = 1; return; }
    if (h != g_seg_base[i]) nk_die_r("nk_ad: text segment patched");
}

/* ---------- S-1 / S-2 微架构标志 + cntvct 自适应标定 ---------- */
static int    g_uarch_flags = 0;                   /* 只翻标志，永不直接判死 */
static double g_cntvct_per_ms = 0.0;

static unsigned long long nk_cntvct(void);

static void nk_cntvct_calibrate(void) {
    if (g_cntvct_per_ms > 0.0) return;
    struct timespec a, b;
    unsigned long long c0 = nk_cntvct();
    clock_gettime(CLOCK_MONOTONIC, &a);
    volatile unsigned long long acc = 0;
    for (int i = 0; i < 20000; i++) { __asm__ __volatile__("" ::: "memory"); acc += (unsigned)i; }
    (void)acc;
    clock_gettime(CLOCK_MONOTONIC, &b);
    unsigned long long c1 = nk_cntvct();
    double ms = (double)(b.tv_sec - a.tv_sec) * 1e3 +
                (double)(b.tv_nsec - a.tv_nsec) / 1e6;
    if (ms < 0.05) return;                          /* 算不准 -> 放过 */
    g_cntvct_per_ms = (double)(c1 - c0) / ms;
    nk_dbg("nk_ad[uarch] cntvct/ms=%.0f\n", g_cntvct_per_ms);
}

static void nk_uarch_update(void) {
    nk_cntvct_calibrate();
    if (g_cntvct_per_ms <= 0.0) return;
    struct timespec a, b;
    unsigned long long c0 = nk_cntvct();
    clock_gettime(CLOCK_MONOTONIC, &a);
    volatile unsigned long long acc = 0;
    for (int i = 0; i < 60000; i++) { __asm__ __volatile__("" ::: "memory"); acc += (unsigned)i; }
    (void)acc;
    clock_gettime(CLOCK_MONOTONIC, &b);
    unsigned long long c1 = nk_cntvct();
    double ms = (double)(b.tv_sec - a.tv_sec) * 1e3 +
                (double)(b.tv_nsec - a.tv_nsec) / 1e6;
    if (ms < 0.05) return;
    double now = (double)(c1 - c0) / ms;
    double dev = (now - g_cntvct_per_ms) / g_cntvct_per_ms;
    if (dev < 0) dev = -dev;
    nk_dbg("nk_ad[uarch] now=%.0f dev=%.2f\n", now, dev);
    if (dev > 0.35) g_uarch_flags |= 1;             /* bit0: 定时器比例漂移 */
}

/* ---------- S-4 syscall 语义抽样：轻量模拟器的分发表最先在边界崩 ---------- */
static void nk_syscall_semantic(void) {
    static const long bogus[] = { 0x7fffL, 0x0e00L, 0x3a00L };
    int bad = 0;
    for (unsigned i = 0; i < sizeof bogus / sizeof *bogus; i++) {
        long r = nk_sys(bogus[i], 0, 0, 0);
        if (r != -38 && r != -1) bad++;             /* -ENOSYS / -EPERM 之外即异常 */
    }
    if (bad) nk_soft_tool("syscall-semantics");
}

/* ---------- S-6 硬件断点槽位枚举（每个 fd 立即 close，绝不缓存） ---------- */
static void nk_hwbp_slot_count(void) {
    if (!nk_root_framework()) return;               /* untrusted_app 会被 SELinux 拒 */
    int got = 0;
    for (int i = 0; i < 8; i++) {
        unsigned char attr[128];
        memset(attr, 0, sizeof attr);
        *(uint32_t *)(attr + 0)  = 5;               /* PERF_TYPE_BREAKPOINT */
        *(uint32_t *)(attr + 4)  = (uint32_t)sizeof attr;
        *(uint64_t *)(attr + 40) = (1ULL << 5) | (1ULL << 6);
        *(uint32_t *)(attr + 52) = 4;               /* HW_BREAKPOINT_X */
        *(uint64_t *)(attr + 56) = (uint64_t)(uintptr_t)&nk_hwbp_slot_count;
        *(uint64_t *)(attr + 64) = 4;
        long fd = nk_sys5(241, (long)attr, 0, -1, -1, 0);
        if (fd < 0) break;
        nk_sys(57, fd, 0, 0);
        got++;
    }
    nk_dbg("nk_ad[tool] hwbp slots available=%d\n", got);
    if (got == 0) return;                           /* 全被拒 -> 权限问题，放过 */
    if (got < 3) nk_soft_tool("hwbp-slots-low");
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
        if (nk_anon_exec())  nk_die_r("nk_ad: exec mapping from memfd");
        if (!nk_svc_ok())    nk_die_r("nk_ad: svc #0 intercepted");
        int jt = nk_jni_table_ok();
        /* P2：ART 会把入口指到 JIT 代码区或 libc helper，硬杀误报太高，降为软分。 */
        if (jt == 0 && !g_jni_soft_done) {     /* 只记一次，不参与累积判死 */
            g_jni_soft_done = 1;
            nk_soft_tool("jni-table-out-of-module");
        }
        if (nk_hwbp_slots()) nk_die_r("nk_ad: hw breakpoint slots occupied");
        nk_tm_verify();
        nk_text_wlock();
        /* v12：P-3 分段自哈希 / P-6+P-4 结构化 maps 判据 / P-5 交叉验证 / S-1 S-4 */
        nk_seg_verify();
        nk_struct_scan();
        nk_peer_verify();
        nk_cross_conflict();
        nk_uarch_update();
        nk_syscall_semantic();
        /* v11.5：行为级验证（ART 入口 / 硬件断点 / 时间源 / 缓存侧信道） */
        nk_behavior_scan();
        /* v11.5：深度行为验证（ART 入口 / 硬件断点 / 时间源 / 缓存侧信道） */
        nk_behavior_scan();
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

/* v11.3 P1：TracerPid 直读统一收敛在这里，并让 NK_AD_ALLOW_DEBUG 覆盖它。
 * 之前 nk_check_full 里那份内联检查没看环境变量，
 * 导致 Android Studio（JDWP，am start -D）附加时即使设了 NK_AD_ALLOW_DEBUG=1 也会 exit 97。 */
static int nk_tracer_pid_foreign(void)
{
    if (getenv("NK_AD_ALLOW_DEBUG")) return 0;
    char st[4096];
    if (nk_slurp("/proc/self/status", st, sizeof st) <= 0) return 0;
    char *t = strstr(st, "TracerPid:");
    if (!t) return 0;
    int pid = (int)strtol(t + 10, NULL, 10);
    nk_dbg("nk_ad[chk] TracerPid=%d wd=%d\n", pid, (int)g_wd_pid);
    if (pid == 0) return 0;
    /* v11.4 P1：只认「真·看门狗 pid」。comm 是可以被 PR_SET_NAME 冒充的，
     * 光比对 comm 等于 nk-wd 就能被外部调试器骗过去（攻击者自己叫 nk-wd 再 attach）。 */
    if (g_wd_pid > 0 && pid == (int)g_wd_pid) return 0;
    if (nk_comm_is((pid_t)pid, "nk-wd"))
        nk_dbg("nk_ad[chk] pid %d 自称 nk-wd 但不是看门狗，判外来 tracer\n", pid);
    return 1;
}

static void nk_check_full(int early) {
    nk_dbg("nk_ad[chk] early=%d wd=%d\n", early, (int)g_wd_pid);

    nk_emu_scan(early);
    nk_tool_scan(early);

    if (nk_any_thread_traced()) nk_die_r("nk_ad: thread traced");

    if (nk_tracer_pid_foreign()) nk_die_r("nk_ad: traced by foreign");

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

__attribute__((visibility("default"))) void NK_AD_SYM(void) {
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
        if (g_inited) {
            nk_wd_reap();       /* P-2：看门狗还在不在 */
            nk_seg_verify();    /* P-3：每轮校验 .text 的 1/8 */
            nk_check_full(0);
        }
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

    /* 基线不在这里采：延后到第一次深检（nk_tm_verify）再做——
     * 既避开 ART 启动期的合法自修改，也省掉 init 阶段对几 MB libc 的哈希。 */

    g_inited = 1;
    nk_cntvct_calibrate();   /* S-2：开机标定本机 cntvct 比例 */
    nk_art_bind();          /* v11.6：给 IR 级 ART 校验准备区间与 JavaVM* */
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
