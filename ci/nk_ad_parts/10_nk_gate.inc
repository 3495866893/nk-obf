/* ============================================================================
 * v12.6 启动闸门
 *   环境检查全过 → 紫色大号横幅「NK 1.0.5 验证通过」→ 3 秒 → 清屏 → 主程序
 *   环境检查不通过 → 由 nk_die_r 打印「请关闭你的调试工具」后多态退出
 * 只在 stdout 是终端时打横幅（管道 / 重定向跳过，避免污染自动化输出）。
 * ========================================================================== */

static int nk_gate_isatty(void) {
    return (isatty(1) != 0) ? 1 : 0;
}

static void nk_gate_w(int fd, const char *s) {
    if (!s || !*s) return;
    nk_sys(64, fd, (long)s, (long)strlen(s));
}

/* 大号 NK 字样 + 版本 + 颜文字，整段一次写出（减少 syscall，避免半屏闪烁） */
static const char NK_GATE_ART[] =
    "\033[35;1m"
    "\n"
    "    ███╗   ██╗ ██╗  ██╗\n"
    "    ████╗  ██║ ██║ ██╔╝\n"
    "    ██╔██╗ ██║ █████╔╝ \n"
    "    ██║╚██╗██║ ██╔═██╗ \n"
    "    ██║ ╚████║ ██║  ██╗\n"
    "    ╚═╝  ╚═══╝ ╚═╝  ╚═╝\n"
    "\n"
    "        1 . 0 . 5   验 证 通 过\n"
    "\n"
    "    (๑•̀ㅂ•́)و✧   ✧◝(⁰▿⁰)◜✧   ヽ(°▽°)ノ\n"
    "\n"
    "\033[0m";

static const char NK_GATE_CLEAR[] = "\033[0m\033[2J\033[H";

static void nk_gate_banner(void) {
    if (!nk_gate_isatty()) return;          /* 非终端：静默放行，不拖 3 秒 */
    nk_gate_w(1, NK_GATE_ART);
    usleep(3 * 1000 * 1000);                /* 停留 3 秒 */
    nk_gate_w(1, NK_GATE_CLEAR);
}

/* 失败提示：即使不是终端也写 stderr，用户重定向日志时同样看得到原因 */
static const char NK_GATE_ART_is_fail[] =
"
[35;1m"
    "      ███╗   ██╗ ██╗  ██╗
"
    "      ████╗  ██║ ██║ ██╔╝
"
    "      ██╔██╗ ██║ █████╔╝ 
"
    "      ██║╚██╗██║ ██╔═██╗ 
"
    "      ██║ ╚████║ ██║  ██╗
"
    "      ╚═╝  ╚═══╝ ╚═╝  ╚═╝
"
    "
"
    "        1 . 0 . 5   环 境 检 查 未 通 过
"
    "
"
    "    (｡•́︿•̀｡)  请关闭调试工具   (╥﹏╥)
"
    "
[0m";

static void nk_gate_fail_hint(const char *why) {
    char b[320];
    int n = snprintf(b, sizeof b,
                     "\033[31;1m"
                     "\n  ✗ NK 1.0.5 环境检查未通过：%s\n"
                     "  请关闭你的调试工具（IDA / Frida / gdb / ptrace 注入 / Xposed 等）后重新运行。\n"
                     "\n\033[0m",
                     why ? why : "unknown");
    if (n > 0) nk_gate_w(2, b);
    if (nk_gate_isatty()) nk_gate_w(1, NK_GATE_ART_is_fail);
}
