/*
 * projects/hello —— NK 混淆流水线的示例工程
 * 编译方式：GUI 里 源目录 = projects/hello，编译单元 = main.c
 * 产物：aarch64 可执行文件（未加壳 → 加了 16 类 IR 变换 + nk_ad 看门狗）
 */
#include <stdio.h>
#include <string.h>

static const char *NK_BANNER = "hello from NK · built by the cloud pipeline";
static const char *NK_ROUTE[] = {"start", "scan", "upload", "obfuscate", "deliver"};

static unsigned int nk_mix(unsigned int x) {
    /* 一个会被 constant_encryption / substitution 处理的常量运算 */
    x ^= 0x9e3779b9u;
    x *= 0x85ebca6bu;
    x ^= x >> 13;
    x *= 0xc2b2ae35u;
    x ^= x >> 16;
    return x;
}

static int nk_stage_index(const char *name) {
    for (int i = 0; i < 5; i++) {
        if (strcmp(name, NK_ROUTE[i]) == 0) return i;
    }
    return -1;
}

int main(int argc, char **argv) {
    puts(NK_BANNER);

    unsigned int acc = 0;
    for (int i = 0; i < 8; i++) acc = nk_mix(acc + (unsigned int) i);
    printf("mix  = 0x%08x\n", acc);

    const char *stage = (argc > 1) ? argv[1] : "deliver";
    int idx = nk_stage_index(stage);
    if (idx < 0) {
        printf("unknown stage: %s\n", stage);
    } else {
        printf("stage  %d/%d: %s\n", idx + 1, 5, NK_ROUTE[idx]);
    }

    /* 一个明显的分支，让 flattening / bcf 有东西可做 */
    if (acc % 3 == 0) puts("path  A");
    else if (acc % 3 == 1) puts("path  B");
    else puts("path  C");

    return 0;
}
