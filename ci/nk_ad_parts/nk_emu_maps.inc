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
