// probe.c —— 反调试环境探针：把三个检测点各自的结果打印出来
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ptrace.h>
#include <sys/prctl.h>

static uint64_t rd_cntvct(void) {
    uint64_t v;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}

int main(int argc, char **argv) {
    printf("=== probe ===\n");
    printf("uid=%d euid=%d\n", (int)getuid(), (int)geteuid());

    // 1) /proc/self/status 关键字段
    FILE *f = fopen("/proc/self/status", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            if (!strncmp(line, "TracerPid:", 10) ||
                !strncmp(line, "State:", 6) ||
                !strncmp(line, "Seccomp:", 8))
                printf("status.%s", line);
        }
        fclose(f);
    } else {
        printf("status: 打不开 /proc/self/status (errno=%d)\n", errno);
    }

    // 2) cntvct 计时（复刻 Nk 的 128 nop 检测）
    for (int round = 0; round < 3; round++) {
        uint64_t a = rd_cntvct();
        for (int i = 0; i < 128; i++) __asm__ __volatile__("nop");
        uint64_t b = rd_cntvct();
        printf("cntvct_delta_round%d=%llu  (阈值 40960)\n", round,
               (unsigned long long)(b - a));
    }

    // 3) ptrace(PTRACE_TRACEME) —— Nk 的核心判定：必须返回 0
    errno = 0;
    long r = ptrace(PTRACE_TRACEME, 0, NULL, NULL);
    int e = errno;
    printf("ptrace_TRACEME=%ld errno=%d (%s)  %s\n", r, e, strerror(e),
           r == 0 ? "-> 通过" : "-> Nk 会在此处 abort!");

    // 4) dumpable / 附加能力
    errno = 0;
    int d = prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);
    printf("prctl_set_dumpable=%d errno=%d\n", d, errno);

    // 5) 再测一次（被 trace 后应失败）
    errno = 0;
    r = ptrace(PTRACE_TRACEME, 0, NULL, NULL);
    printf("ptrace_TRACEME_2nd=%ld errno=%d (%s)\n", r, errno, strerror(errno));

    printf("=== probe done ===\n");
    return 0;
}
