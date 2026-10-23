// probe2.c —— 复刻 Ensia 的 "violent exit" 序列，单独验证它在真机上会不会被外部 SIGKILL
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <sys/prctl.h>

// 和 getViolentExitAsm(aarch64) 一模一样的头两步
static void nk_exit_101(void) {
    register long x8 __asm__("x8") = 167;  // prctl
    register long x0 __asm__("x0") = 4;    // PR_SET_DUMPABLE
    register long x1 __asm__("x1") = 0;
    register long x2 __asm__("x2") = 0;
    __asm__ __volatile__("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    x8 = 94;   // exit_group
    x0 = 101;  // exitCode
    __asm__ __volatile__("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    // 如果 exit_group 竟然返回了，就继续往下撞
    *(volatile int *)0 = 1;
    for (;;) { }
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "info";

    if (!strcmp(mode, "abort")) {
        printf("mode=abort：调用 Ensia 同款 prctl+exit_group(101)\n");
        fflush(stdout);
        nk_exit_101();
        printf("!! exit_group 居然返回了\n");
        return 9;
    }
    if (!strcmp(mode, "spin")) {
        printf("mode=spin：100%% CPU 死循环，看外部是否杀我们\n");
        fflush(stdout);
        volatile long a = 1;
        for (;;) a = a * 1103 + 12345;
        return 0;
    }
    if (!strcmp(mode, "traceme2")) {
        errno = 0;
        long r1 = ptrace(PTRACE_TRACEME, 0, 0, 0);
        int e1 = errno;
        errno = 0;
        long r2 = ptrace(PTRACE_TRACEME, 0, 0, 0);
        int e2 = errno;
        printf("1st TRACEME=%ld errno=%d | 2nd TRACEME=%ld errno=%d\n", r1, e1, r2, e2);
        return 0;
    }

    printf("mode=info\nuid=%d\n", (int)getuid());
    FILE *f = fopen("/proc/self/status", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f))
            if (!strncmp(line, "TracerPid:", 10) || !strncmp(line, "Seccomp:", 8))
                printf("%s", line);
        fclose(f);
    }
    errno = 0;
    long r = ptrace(PTRACE_TRACEME, 0, 0, 0);
    printf("TRACEME=%ld errno=%d\n", r, errno);
    printf("用法: %s [abort|spin|traceme2|info]\n", argv[0]);
    return 0;
}
