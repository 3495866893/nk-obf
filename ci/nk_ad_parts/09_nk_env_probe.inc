/* ---------- v12.2：环境行为探针（E-2 / E-4 / E-6 / N-1 / N-2 / N-3） ----------
 * 原则不变：新判据先软分，读不到一律放过（fail-open）。
 */
#include <sys/auxv.h>
#include <dlfcn.h>

static uint64_t g_boot_ms = 0;
static uint64_t nk_boot_age_ms(void) {
    if (g_boot_ms == 0) {
        struct timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
            g_boot_ms = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
        else
            return 0;
    }
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    uint64_t now = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
    return now > g_boot_ms ? now - g_boot_ms : 0;
}

/* ---------- E-2：主进程反向占坑（证明看门狗没被别人先 ptrace 住） ----------
 * 做法：自己 PTRACE_SEIZE 看门狗 -> INTERRUPT -> wait4(WNOHANG) 证明它可以被调度
 * -> DETACH 立刻放开。成功 = 空闲；EPERM = 已有别的 tracer 占着（硬证据）；
 * ESRCH = 进程没了（交给 nk_wd_reap）。整段只在深检里跑，且失败只记软分。
 */
#define NK_PTRACE_SEIZE     0x4206L
#define NK_PTRACE_INTERRUPT 0x4207L
#define NK_PTRACE_DETACH    0x11L
#define NK_PTRACE_CONT      0x07L

static void nk_wd_reseize(void) {
    if (g_wd_pid <= 0) return;
    long r = nk_sys5(117 /* ptrace */, NK_PTRACE_SEIZE, (long)g_wd_pid, 0, 0, 0);
    if (r < 0) {
        if (r == -1)        nk_soft_tool("wd-seized-by-other");   /* EPERM */
        else if (r == -3)   nk_soft_tool("wd-gone");              /* ESRCH */
        else                nk_dbg("nk_ad[e2] seize rc=%ld\n", r);
        return;
    }
    nk_sys5(117, NK_PTRACE_INTERRUPT, (long)g_wd_pid, 0, 0, 0);
    int st = 0;
    (void)waitpid(g_wd_pid, &st, WNOHANG);                        /* 只证明可调度 */
    nk_sys5(117, NK_PTRACE_CONT, (long)g_wd_pid, 0, 0, 0);
    nk_sys5(117, NK_PTRACE_DETACH, (long)g_wd_pid, 0, 0, 0);
    nk_dbg("nk_ad[e2] watchdog %d seize/detach ok\n", (int)g_wd_pid);
}

/* ---------- E-4：给 g_uarch_flags 接消费点（以前只翻位、没人读） ---------- */
static int g_uarch_soft_done = 0;
static void nk_uarch_consume(void) {
    if (!(g_uarch_flags & 1)) return;
    /* 定时器比例漂移 + 已有两条真·模拟器证据 -> 才算够格硬处置；
     * 单独一条漂移只在部分机型（省电调频）出现，绝不判死。 */
    if (g_emu_score >= 3) nk_die_r("nk_ad: timer desync + emulation evidence");
    if (!g_uarch_soft_done) { g_uarch_soft_done = 1; nk_soft_tool("uarch-timer-drift"); }
}

/* ---------- E-6：property 第二信源（不依赖文件内容） ---------- */
typedef int (*nk_prop_fn)(const char *, char *);
static int nk_prop_get(const char *k, char *out, int cap) {
    static nk_prop_fn fn = NULL;
    static int tried = 0;
    if (!tried) {
        tried = 1;
        fn = (nk_prop_fn)dlsym(RTLD_DEFAULT, "__system_property_get");
    }
    if (!fn || cap <= 1) return -1;
    out[0] = 0;
    int n = fn(k, out);
    if (n <= 0 || n >= cap) return -1;
    return n;
}

/* ---------- N-3：自身安装域校验（反重打包的第一步） ---------- */
static void nk_self_domain(void) {
    char exe[256];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return;                                   /* 读不到 -> 放过 */
    exe[n] = 0;
    static const char *ok[] = {
        "/data/app/", "/data/local/", "/data/data/", "/data/user/",
        "/system/", "/vendor/", "/apex/", "/product/", "/odm/", "/mnt/", NULL
    };
    for (int i = 0; ok[i]; i++)
        if (!strncmp(exe, ok[i], strlen(ok[i]))) return;
    nk_dbg("nk_ad[n3] self=%s\n", exe);
    nk_soft_tool("self-path-outside-install");
}

/* ---------- E-6 + N-1 + N-2 合并入口 ---------- */
static void nk_env_probe(void) {
    char v[128];

    /* E-6：ro.kernel.qemu / ro.hardware / ro.product.cpu.abi 作为第二信源，
     *      与 build.prop、设备节点各自独立；单条只软分。 */
    if (nk_prop_get("ro.kernel.qemu", v, sizeof v) > 0 && v[0] == '1')
        nk_soft_tool("prop-qemu");
    if (nk_prop_get("ro.hardware", v, sizeof v) > 0 &&
        (strstr(v, "ranchu") || strstr(v, "goldfish")))
        nk_soft_tool("prop-ranchu");
    if (nk_prop_get("ro.product.cpu.abi", v, sizeof v) > 0 && strncmp(v, "arm64", 5) != 0)
        nk_soft_tool("prop-abi-mismatch");

    /* N-1：seccomp 自检（Android 8+ 自带过滤器，所以只写日志，不打分） */
    long sc = nk_sys5(167 /* prctl */, 21 /* PR_GET_SECCOMP */, 0, 0, 0, 0);
    nk_dbg("nk_ad[n1] seccomp=%ld\n", sc);

    /* N-2：PAC / BTI 可用性实测（同样只写日志；缺 PAC 不等于被改，老 SoC 本来就没有） */
    unsigned long hwcap = getauxval(AT_HWCAP);
    unsigned long hwcap2 = getauxval(AT_HWCAP2);
    int pac = (hwcap & (1UL << 30)) || (hwcap & (1UL << 31));
    int bti = (hwcap2 & (1UL << 17)) != 0;
    nk_dbg("nk_ad[n2] hwcap=%lx hwcap2=%lx pac=%d bti=%d\n", hwcap, hwcap2, pac, bti);

    /* N-3：自身路径是否落在合法安装域 */
    nk_self_domain();
}
