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
            nk_hint_die(1, "nk_ad: critical module .text patched");
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
    nk_hint_die(1, "nk_ad: art jni table corrupted");
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

/* v12：行为检查里用到的低频判据，定义在后面，这里先声明 */
static void nk_thread_anomaly(void);
static void nk_hwbp_slot_count(void);
static void nk_cache_ratio(void);
static void nk_cpuinfo_conflict(void);
