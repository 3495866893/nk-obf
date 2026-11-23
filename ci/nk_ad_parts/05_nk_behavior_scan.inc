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
    nk_cache_ratio();          /* S-3：随机 vs 顺序的比值，抗降频噪声 */
    nk_cpuinfo_conflict();     /* S-5：cpuinfo / MIDR 与环境是否自洽 */
    /* v12：低频项（线程枚举 + 槽位枚举）只在行为检查里跑，避免 1.5 s 一轮的开销 */
    nk_thread_anomaly();
    nk_hwbp_slot_count();
}


/* =====================================================================
 * S-3 / S-5（v12.1）
 * 两条都只记软分：它们是「像模拟器」的旁证，不单独构成判死理由。
 * ===================================================================== */

static unsigned long long nk_ns_now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (unsigned long long)ts.tv_sec * 1000000000ULL + (unsigned long long)ts.tv_nsec;
}

/* S-3：同一块内存分别按「顺序」和「打乱顺序」各走 512 步，比较两者耗时。
 * 真机上随机访问会明显更慢（cache line / TLB 不命中）；软件模拟器的内存访问
 * 是字典查找，随机与顺序几乎同速。比单一阈值更抗 CPU 降频与调度噪声。
 * 每轮种子都不同，静态分析无法预知访问序列。 */
static void nk_cache_ratio(void) {
    const size_t SZ = 4u << 20;      /* 4 MB：跨过典型 L2，落在 L3 / 内存 */
    const int N = 512;
    const int ROUNDS = 3;
    unsigned char *buf = (unsigned char *)mmap(NULL, SZ, PROT_READ | PROT_WRITE,
                                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) return;

    /* 先触碰每一页：把缺页开销挡在计时之外 */
    for (size_t i = 0; i < SZ; i += 4096) buf[i] = (unsigned char)(i >> 12);

    unsigned long long best_seq = ~0ULL, best_rnd = ~0ULL;
    for (int r = 0; r < ROUNDS; r++) {
        unsigned long long t0 = nk_ns_now();
        volatile unsigned long long acc = 0;
        for (int i = 0; i < N; i++) acc += buf[(size_t)i * 8192 % SZ];
        unsigned long long t1 = nk_ns_now();

        unsigned int seed = (unsigned int)(nk_ns_now() & 0xffffffu) | 1u;
        unsigned long long t2 = nk_ns_now();
        for (int i = 0; i < N; i++) {
            seed = seed * 1103515245u + 12345u;
            size_t off = (size_t)((seed >> 9) % (SZ / 4096)) * 4096;
            acc += buf[off];
        }
        unsigned long long t3 = nk_ns_now();
        if (acc == 0x1234567890abcdefULL) nk_dbg("");   /* 防止整段被优化掉 */

        if (t1 > t0 && t1 - t0 < best_seq) best_seq = t1 - t0;
        if (t3 > t2 && t3 - t2 < best_rnd) best_rnd = t3 - t2;
    }
    munmap(buf, SZ);
    if (best_seq == 0 || best_rnd == 0) return;

    /* 随机不足顺序的 1.08 倍 -> 没有缓存惩罚，像模拟器 */
    if (best_rnd * 100ULL < best_seq * 108ULL) nk_soft_tool("cache-no-penalty");

    /* 单次随机访问低到物理上不可能（< 6 ns） */
    unsigned long long per = best_rnd / (unsigned long long)N;
    if (per > 0 && per < 6ULL) nk_soft_tool("cache-latency-too-low");
}

/* S-5：三条独立信源互相印证，模拟器最难同时伪装。
 * 1) /proc/cpuinfo 结构是否完整（真机必有 CPU implementer）
 * 2) 是否出现模拟器 / 交叉架构痕迹
 * 3) processor 行数与 sysconf 的在线核数是否自洽
 * 4) MIDR_EL1 的 implementer 是否落在真实 ARM 厂商集合内 */
static void nk_cpuinfo_conflict(void) {
    static char ci[131072];
    int n = nk_slurp("/proc/cpuinfo", ci, sizeof ci - 1);
    if (n <= 0) { nk_soft_tool("cpuinfo-unreadable"); return; }
    ci[n] = 0;

    if (!strstr(ci, "implementer")) nk_soft_tool("cpuinfo-no-implementer");

    static const char *bad[] = {"qemu", "goldfish", "ranchu", "Android Emulator",
                                "GenuineIntel", "sun4i", "VirtualBox", "VMware"};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        if (strstr(ci, bad[i])) { nk_soft_tool("cpuinfo-emulator-token"); break; }
    }

    int lines = 0;
    for (const char *p = ci; (p = strstr(p, "processor")) != NULL; p++) lines++;
    long onln = sysconf(_SC_NPROCESSORS_ONLN);
    if (onln > 0 && lines > 0) {
        if (lines > onln * 2 + 2 || onln > lines * 2 + 2) nk_soft_tool("cpu-count-mismatch");
    }

    char mid[64];
    int m = nk_slurp("/sys/devices/system/cpu/cpu0/regs/identification/midr_el1", mid, sizeof mid - 1);
    if (m > 0) {
        mid[m] = 0;
        unsigned long long v = strtoull(mid, NULL, 0);
        unsigned impl = (unsigned)((v >> 24) & 0xffu);
        if (v == 0) {
            nk_soft_tool("midr-zero");
        } else if (impl != 0x41 && impl != 0x48 && impl != 0x4d && impl != 0x51 &&
                   impl != 0x53 && impl != 0x61 && impl != 0x6e) {
            nk_soft_tool("midr-implausible");
        }
    }
}

/* =====================================================================
 * v12 加固块（P-2/P-3/P-4/P-5/P-6 + S 系列）
 * 全部新判据先软分；只有「已确立的硬证据」才 nk_die_r。
 * ===================================================================== */
