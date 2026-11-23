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
