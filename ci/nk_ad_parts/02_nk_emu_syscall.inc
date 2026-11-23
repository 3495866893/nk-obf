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
        nk_emu_hit(1, "cntfrq");
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
        if (ratio < 0.25 || ratio > 4.0) nk_emu_note("timer-ratio");
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
    if (g_emu_kind == 0) nk_emu_note("segv-not-delivered");
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
        nk_emu_note("comm-readback");
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
