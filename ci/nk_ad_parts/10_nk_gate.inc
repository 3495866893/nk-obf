/* ============================================================================
 * v12.6 启动闸门
 *   环境检查全过 -> 紫色大号横幅「NK 1.0.5 验证通过」-> 3 秒 -> 清屏 -> 主程序
 *   检查不通过   -> 打印「请关闭你的调试工具」后多态退出（nk_die_r 里调用）
 * 只在 stdout 是终端时打横幅（管道 / 重定向跳过，避免污染自动化输出）。
 * ========================================================================== */

static int nk_gate_isatty(void) {
    return (isatty(1) != 0) ? 1 : 0;
}

static void nk_gate_w(int fd, const char *s) {
    if (!s || !*s) return;
    nk_sys(64, fd, (long)s, (long)strlen(s));
}

static const char NK_GATE_ART[] =
    "\033[35;1m"
    "\n"
    "    ███╗   ██╗ ██╗  ██╗\n"
    "    ████╗  ██║ ██║ ██╔╝\n"
    "    ██╔██╗ ██║ █████╔╝\n"
    "    ██║╚██╗██║ ██╔═██╗\n"
    "    ██║ ╚████║ ██║  ██╗\n"
    "    ╚═╝  ╚═══╝ ╚═╝  ╚═╝\n"
    "\n"
    "        1 . 0 . 5   验 证 通 过\n"
    "\n"
    "    (๑•̀ㅂ•́)و✧    ✧◝(⁰▿⁰)◜✧    ヽ(°▽°)ノ\n"
    "\n"
    "\033[0m";

static const char NK_GATE_ART_FAIL[] =
    "\033[35;1m"
    "\n"
    "    ███╗   ██╗ ██╗  ██╗\n"
    "    ████╗  ██║ ██║ ██╔╝\n"
    "    ██╔██╗ ██║ █████╔╝\n"
    "    ██║╚██╗██║ ██╔═██╗\n"
    "    ██║ ╚████║ ██║  ██╗\n"
    "    ╚═╝  ╚═══╝ ╚═╝  ╚═╝\n"
    "\n"
    "        1 . 0 . 5   检 查 未 通 过\n"
    "\n"
    "    (｡•́︿•̀｡)   请 关 闭 调 试 工 具    (╥﹏╥)\n"
    "\n"
    "\033[0m";

static const char NK_GATE_CLEAR[] = "\033[0m\033[2J\033[H";

/* 默认开；NK_AD_GATE=0 关闭（自动化测试用） */
static int nk_gate_on(void) {
    const char *e = getenv("NK_AD_GATE");
    return (e && e[0] == '0') ? 0 : 1;
}

static void nk_gate_banner(void) {
    if (!nk_gate_on()) return;
    nk_gate_w(1, NK_GATE_ART);              /* 终端 / 重定向都打横幅 */
    if (nk_gate_isatty()) {
        usleep(3 * 1000 * 1000);            /* 终端里停留 3 秒 */
        nk_gate_w(1, NK_GATE_CLEAR);        /* 然后清屏进主程序 */
    }
}

/* 失败提示：即使不是终端也写 stderr，用户重定向日志时同样看得到原因 */
static void nk_gate_fail_hint(const char *why) {
    char b[320];
    int n = snprintf(b, sizeof b,
                     "\033[31;1m"
                     "\n  x NK 1.0.5 环境检查未通过：%s\n"
                     "  请关闭你的调试工具（IDA / Frida / gdb / ptrace 注入 / Xposed 等）后重新运行。\n"
                     "\n\033[0m",
                     why ? why : "unknown");
    if (n > 0) nk_gate_w(2, b);
    if (nk_gate_isatty()) nk_gate_w(1, NK_GATE_ART_FAIL);
}
