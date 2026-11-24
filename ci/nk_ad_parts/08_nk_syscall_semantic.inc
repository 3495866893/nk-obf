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
        nk_cbase_check();
        nk_struct_scan();
        nk_peer_verify();
        nk_cross_conflict();
        nk_uarch_update();
        nk_uarch_consume();      /* E-4：漂移+模拟器证据合判 */
        nk_wd_reseize();         /* E-2：反向占坑证看门狗空闲 */
        nk_env_probe();          /* E-6 / N-1 / N-2 / N-3 */
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
            if (g_emu_score >= 4) nk_die_r("nk_ad: emulation score>=4");
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
    if (nk_tracer_is_ours(pid)) return 0;
    if (nk_comm_is((pid_t)pid, "nk-wd")) {
        /* Q-3：看门狗 fork 出来那一两秒里 g_wd_pid 可能还没落盘，
         * comm 对得上就先放过，否则启动期会被自己人判死。 */
        if (g_wd_pid <= 0 && nk_boot_age_ms() < 8000) {
            nk_dbg("nk_ad[chk] pid %d 自称 nk-wd 且 wd 未知，启动窗口内放过\n", pid);
            return 0;
        }
        nk_dbg("nk_ad[chk] pid %d 自称 nk-wd 但不是看门狗，判外来 tracer\n", pid);
    }
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
            nk_seg_verify();    /* P-3：每轮校验 .text 的 1/8（运行时基线） */
            nk_cbase_check();   /* P-3：再校验 1/64 段（编译期基线，全量覆盖） */
            nk_check_full(0);
        }
    }
    return NULL;
}

/* v12.5：看门狗不再 ptrace 主进程。
 *
 * 原来用 PTRACE_SEIZE 会把主进程留在 ptrace-stop 里（/proc/self/status 出现 State: t），
 * 主进程自己的 nk_trace_state() 就会把自己的看门狗当成入侵者 —— 真机必杀。
 * 现在改成「只读 /proc/<父>/status 的 TracerPid」：父进程一旦被任何人 attach，
 * 看门狗立刻 SIGKILL 父进程；主进程自己也会在深检里发现 TracerPid 异常。
 * 双向覆盖，且完全不改变父进程的调度状态。 */
static int nk_self_tracerpid(pid_t p) {
    char path[64], b[4096];
    snprintf(path, sizeof path, "/proc/%d/status", (int)p);
    if (nk_slurp(path, b, (int)sizeof b - 1) <= 0) return -1;
    char *t = strstr(b, "TracerPid:");
    if (!t) return -1;
    return (int)strtol(t + 10, NULL, 10);
}

static void nk_watchdog(pid_t target) {
    prctl(PR_SET_NAME, "nk-wd", 0, 0, 0);
    for (;;) {
        usleep(300 * 1000);
        if (kill(target, 0) != 0) _exit(0);          /* 父进程没了，收工 */
        int tp = nk_self_tracerpid(target);
        if (tp > 0) {                                /* 任何 tracer 都是外来的（我们不再 ptrace） */
            kill(target, 9);
            _exit(0);
        }
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
    nk_gate_banner();   /* v12.6：环境全过 -> 紫色横幅 3 秒 -> 清屏 */

    pid_t self = getpid();
    pid_t wd = fork();
    if (wd == 0) {
        nk_watchdog(self);
        _exit(0);
    }
    if (wd > 0) {
        g_wd_pid = wd;                                  /* v12.5：fork 返回即认亲，无需等 SEIZE */
        g_wd_start = nk_proc_starttime(wd);
        nk_dbg("nk_ad[init] watchdog=%d（非 ptrace 看护，父进程状态不受影响）\n", (int)wd);
    }

    pthread_t th;
    if (pthread_create(&th, NULL, nk_thread, NULL) == 0) pthread_detach(th);
}
