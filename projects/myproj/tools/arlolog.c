/* Arlo 日志解密工具 v2.0 — 解密输出到 .dec 文件
 * 编译: gcc -o arlolog arlolog.c -O2 -Wall
 * 用法: ./arlolog                          → 解密全部到 .dec 文件
 *        ./arlolog xxx.enc                  → 解密单个到 xxx.dec
 *        ./arlolog -                        → 解密全部并输出到终端(调试用)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>

#define LOG_PATH "/data/local/tmp/.arlo_log"
#define LOG_KEY_LEN 16

static const unsigned char LOG_KEY[] = {
    0x5A, 0x3F, 0x8C, 0x12, 0x7E, 0x4D, 0x9A, 0x2B,
    0x6F, 0x31, 0xE8, 0x55, 0x0D, 0x7B, 0xC3, 0x19
};

static void decrypt_buf(const unsigned char* in, unsigned char* out, int len) {
    for (int i = 0; i < len; i++) {
        out[i] = in[i] ^ LOG_KEY[0];
    }
}

// 去掉 .enc 后缀，换成 .dec
static void make_dec_path(const char* enc_path, char* dec_path, int sz) {
    snprintf(dec_path, sz, "%s", enc_path);
    int len = strlen(dec_path);
    if (len > 4 && strcmp(dec_path + len - 4, ".enc") == 0) {
        strcpy(dec_path + len - 4, ".dec");
    } else {
        // 不是 .enc 结尾，直接追加 .dec
        snprintf(dec_path, sz, "%s.dec", enc_path);
    }
}

// 解密单个文件 → .dec，返回成功/失败
static int decrypt_to_file(const char* enc_path, FILE* fallback) {
    FILE* fin = fopen(enc_path, "rb");
    if (!fin) {
        fprintf(fallback ? fallback : stdout, "[!] 无法打开: %s\n", enc_path);
        return 0;
    }

    char dec_path[512];
    make_dec_path(enc_path, dec_path, sizeof(dec_path));

    FILE* fout = fopen(dec_path, "wb");
    if (!fout) {
        fprintf(fallback ? fallback : stdout, "[!] 无法创建: %s\n", dec_path);
        fclose(fin);
        return 0;
    }

    unsigned char buf[4096];
    unsigned char dec[4096];
    int n;
    long total = 0;

    while ((n = fread(buf, 1, sizeof(buf), fin)) > 0) {
        decrypt_buf(buf, dec, n);
        fwrite(dec, 1, n, fout);
        total += n;
    }

    fclose(fin);
    fclose(fout);

    fprintf(fallback ? fallback : stdout, "[OK] %s → %s (%ld bytes)\n", enc_path, dec_path, total);
    return 1;
}

static void decrypt_all(const char* dir_path, int to_file) {
    DIR* d = opendir(dir_path);
    if (!d) {
        printf("[!] 日志目录不存在: %s\n", dir_path);
        printf("    请确保程序已至少运行过一次\n");
        return;
    }

    struct dirent* entry;
    char files[256][256];
    int file_count = 0;

    while ((entry = readdir(d)) != NULL && file_count < 256) {
        if (strstr(entry->d_name, ".enc")) {
            snprintf(files[file_count], sizeof(files[file_count]), "%s", entry->d_name);
            file_count++;
        }
    }
    closedir(d);

    if (file_count == 0) {
        printf("[!] 没有找到加密日志文件\n");
        return;
    }

    printf("找到 %d 个加密日志，解密中...\n\n", file_count);

    int ok = 0;
    for (int i = 0; i < file_count; i++) {
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, files[i]);

        if (to_file) {
            ok += decrypt_to_file(full_path, stdout);
        } else {
            // 输出到终端
            FILE* f = fopen(full_path, "rb");
            if (!f) continue;
            printf("===== %s =====\n", full_path);
            unsigned char buf[4096], dec[4096];
            int n;
            while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
                decrypt_buf(buf, dec, n);
                fwrite(dec, 1, n, stdout);
            }
            fclose(f);
            printf("\n===== END =====\n\n");
            ok++;
        }
    }

    printf("\n完成: %d/%d 解密成功\n", ok, file_count);
    if (to_file && ok > 0) {
        printf("解密文件已保存到 %s/*.dec\n", dir_path);
    }
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "-") == 0) {
        // 调试模式，输出到终端
        decrypt_all(LOG_PATH, 0);
    } else if (argc > 1) {
        // 解密指定文件
        decrypt_to_file(argv[1], stdout);
    } else {
        // 默认：解密全部到 .dec 文件
        decrypt_all(LOG_PATH, 1);
    }
    return 0;
}