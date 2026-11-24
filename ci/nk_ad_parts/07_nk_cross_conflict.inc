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
    if (h != g_seg_base[i]) nk_hint_die(1, "nk_ad: text segment patched");
}

/* ---------- P-3 编译期基线（链接后由 ci/tools/nk_patch_baseline.py 回填）----------
 * 运行期把主模块代码段按 NK_CSEG 等分，轮转比对编译期算出的哈希：
 *   - 值为 0 的段 = 构建时没回填（本地手工编、或脚本没跑），直接放过（fail-open）；
 *   - 带重定位的段由脚本置豁免位，这里跳过；
 *   - 覆盖范围从「前 1 MB」变成整段 .text，成本按 1/NK_CSEG 摊平。
 * 这两个数组必须留名字（有 used 属性），脚本按符号名定位写入口。
 */
#define NK_CSEG 64
/* 哨兵 1 = 未回填。必须非零，否则整块落进 .bss（NOBITS），
 * ci/tools/nk_patch_baseline.py 就没有可写的文件偏移，只能跳过。
 * 段名单独指定，保证它是 PROGBITS 且不与别的常量并段。 */
unsigned long __nk_base_hash[NK_CSEG]
    __attribute__((used, section(".nkbase"), aligned(8))) = {1};
unsigned char __nk_base_exempt[(NK_CSEG + 7) / 8]
    __attribute__((used, section(".nkbase_ex"), aligned(8))) = {0xFF};

static uint64_t g_ctext_lo = 0;
static uint64_t g_ctext_len = 0;
static int      g_cseg_cur = 0;

static int nk_ctext_cb(struct dl_phdr_info *info, size_t sz, void *data)
{
    (void)sz; (void)data;
    const char *nm = info->dlpi_name ? info->dlpi_name : "";
    if (nm[0] != 0) return 1;              /* 只认主模块（dl_iterate_phdr 第一个） */
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type == PT_LOAD && (ph->p_flags & PF_X) && ph->p_memsz) {
            g_ctext_lo  = (uint64_t)info->dlpi_addr + ph->p_vaddr;
            g_ctext_len = (uint64_t)ph->p_memsz;
            return 1;
        }
    }
    return 1;
}

static void nk_ctext_bind(void)
{
    if (g_ctext_lo || g_ctext_len) return;
    dl_iterate_phdr(nk_ctext_cb, NULL);
    nk_dbg("nk_ad[base] text lo=0x%llx len=%llu\n",
           (unsigned long long)g_ctext_lo, (unsigned long long)g_ctext_len);
}

static void nk_cbase_check(void)
{
    nk_ctext_bind();
    if (!g_ctext_lo || g_ctext_len < (uint64_t)NK_CSEG * 256) return;
    int i = g_cseg_cur;
    g_cseg_cur = (g_cseg_cur + 1) % NK_CSEG;
    if (__nk_base_hash[i] == 0 || __nk_base_hash[i] == 1)
        return;                                  /* 未回填（0 或哨兵 1）-> 放过 */
    if (__nk_base_exempt[i / 8] & (unsigned char)(1u << (i % 8))) return;
    uint64_t per = g_ctext_len / NK_CSEG;
    uint64_t off = (uint64_t)i * per;
    uint64_t len = (i == NK_CSEG - 1) ? (g_ctext_len - off) : per;
    uint64_t h = nk_hash_mem((const unsigned char *)(g_ctext_lo + off), len);
    if (h != (uint64_t)__nk_base_hash[i]) {
        nk_dbg("nk_ad[base] seg %d 期望 %llx 实际 %llx\n", i,
               (unsigned long long)__nk_base_hash[i], (unsigned long long)h);
        nk_hint_die(1, "nk_ad: text chunk differs from build baseline");
    }
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
