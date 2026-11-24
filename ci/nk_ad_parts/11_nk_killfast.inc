/* ===================== v12.7 冻结即终止 =====================
 * 用户要求：「检测到问题应该冻结进程不继续加载，然后退出或立即退出」。
 *
 * 为什么不是一句 _exit()：
 *   - _exit() 要走内核退出路径，期间其它线程仍在跑（可能已经把关键数据
 *     解密出来、把 main 的后续静态构造器跑完）；
 *   - 攻击者可以挂 exit hook / atexit / 在 _exit 前后插桩观察。
 *
 * 做法（两次信号，进程级生效，所有线程一起停）：
 *   1) fork 一个杀手子进程：它先睡 ~120ms 让父进程的 SIGSTOP 生效，
 *      再按轮换选出的一种信号把父进程打死；
 *   2) 父进程立刻 PR_SET_DUMPABLE=0（禁止别人 ptrace/gcore）+ SIGSTOP 自杀，
 *      SIGSTOP 是进程级的 → 全部线程就地停住，不再执行任何初始化/加载/解密；
 *   3) 若有人把父进程 CONT 起来（调试器），父进程还有一条 _exit 兜底。
 *
 * 被杀方式轮换（KILL / SEGV / ABRT / ILL）：静态上看不到唯一出口，
 * 也不会每次都留同一种 signal 特征。 */

static pid_t g_fk_n = 0;

static void nk_freeze_kill(void)
{
    pid_t me = getpid();
    unsigned n = (unsigned)__atomic_add_fetch(&g_fk_n, 1, __ATOMIC_RELAXED);

    /* 先把内核态的自证能力关掉：core dump / ptrace 一律不允许 */
    (void)prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);

    /* 杀手子进程 */
    pid_t kid = fork();
    if (kid == 0) {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 120L * 1000L * 1000L;      /* 120 ms */
        (void)nanosleep(&ts, 0);
        switch (n % 4u) {
            case 0:  (void)kill(me, SIGKILL); break;   /* 立刻消失，无墓碑 */
            case 1:  (void)kill(me, SIGSEGV); break;   /* 看起来像正常崩溃 */
            case 2:  (void)kill(me, SIGABRT); break;   /* abort 特征 */
            default: (void)kill(me, SIGILL);  break;   /* 非法指令特征 */
        }
        _exit(0);
    }

    /* 父进程：就地冻结。SIGSTOP 不可捕获、不可忽略，全进程线程一起停。 */
    (void)kill(me, SIGSTOP);

    /* 走到这里说明有人 CONT 了我们（调试器/看门狗）—— 兜底直接退出。 */
    _exit(NK_EXIT);
    for (;;) _exit(NK_EXIT);
}
