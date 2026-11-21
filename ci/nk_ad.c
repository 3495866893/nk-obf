// nk_ad.c v3 —— Ensia 反调试强化模块（arm64 / Android，纯用户态）
// 原则：fail-open。读不到/权限不足一律放行，只在确定性证据上退出(_exit(97))。
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

static void nk_check_full(int early) {
    nk_dbg("nk_ad[chk] early=%d wd=%d\n", early, (int)g_wd_pid);

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
