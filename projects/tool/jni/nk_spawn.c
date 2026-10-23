// nk_spawn.c —— 用 ptrace 全程跟踪启动目标，用于验证 TracerPid 检测是否生效
// 用法: nk_spawn <程序> [参数...]
//   子进程 TRACEME 后 exec，父进程保持跟踪 → 目标进程的 TracerPid 非 0
//   期望：带反调试的目标检测到后 exit(97)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "用法: %s <程序> [参数...]\n", argv[0]);
        return 2;
    }
    pid_t pid = fork();
    if (pid == 0) {
        if (ptrace(PTRACE_TRACEME, 0, 0, 0) == -1) {
            perror("TRACEME");
            _exit(126);
        }
        raise(SIGSTOP);              // 等父进程接管
        execv(argv[1], &argv[1]);
        perror("execv");
        _exit(127);
    }
    if (pid < 0) { perror("fork"); return 2; }

    int status = 0;
    waitpid(pid, &status, 0);        // SIGSTOP
    ptrace(PTRACE_CONT, pid, 0, 0);  // 让它继续跑到 exec

    int code = -1;
    for (;;) {
        if (waitpid(pid, &status, 0) < 0) break;
        if (WIFEXITED(status)) { code = WEXITSTATUS(status); break; }
        if (WIFSIGNALED(status)) {
            printf("[nk_spawn] 目标被信号 %d 终止\n", WTERMSIG(status));
            code = 128 + WTERMSIG(status);
            break;
        }
        if (WIFSTOPPED(status)) ptrace(PTRACE_CONT, pid, 0, WSTOPSIG(status));
    }
    printf("[nk_spawn] 目标退出码 = %d\n", code);
    if (code == 97)
        printf("[nk_spawn] ★ 反调试生效：检测到被 trace 并静默退出\n");
    else if (code == 0)
        printf("[nk_spawn] 未检测到调试（=0），反调试可能没生效\n");
    return 0;
}
