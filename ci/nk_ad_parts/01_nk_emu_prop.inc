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
    if (platform == 0 || random == 0) nk_emu_hit(0, "auxv-platform");
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
