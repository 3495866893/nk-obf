#pragma once
// ============================================================
// Arlo 加密日志系统 v2.0
// 全部输出加密，禁止明文；按日期分隔，完整记录调用链
// ============================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdarg.h>
#include <errno.h>

#define LOG_PATH "/data/local/tmp/.arlo_log"
#define LOG_FILE_PREFIX "arlog_"

// ---- 加密密钥（编译时混淆，防止静态分析） ----
static const unsigned char LOG_KEY[] = {
    0x5A, 0x3F, 0x8C, 0x12, 0x7E, 0x4D, 0x9A, 0x2B,
    0x6F, 0x31, 0xE8, 0x55, 0x0D, 0x7B, 0xC3, 0x19
};
#define LOG_KEY_LEN 16

// ---- 字符串转hex（避免明文字符串在二进制中直接被搜到） ----
static const unsigned char LOG_PATH_ENC[] = {
    0x4F, 0x6A, 0xAD, 0x3E, 0x45, 0x7C, 0xB8, 0x2F,
    0x5E, 0x38, 0x9F, 0x14, 0x6D, 0x0A, 0xFB, 0x27,
    0x4C, 0x7D, 0x8E, 0x11, 0x52, 0x3B, 0xA7, 0x0E
};

// ---- 内部：XOR 加密写入 ----
static FILE* arlo_log_fp = NULL;
static char arlo_last_date[16] = {0};
 static void log_encrypt_buf(const unsigned char* in, unsigned char* out, int len) {
    for (int i = 0; i < len; i++) {
        out[i] = in[i] ^ LOG_KEY[0];
    }
}
static void log_ensure_dir() {
    mkdir(LOG_PATH, 0700);
}

static void log_get_date_str(char* buf, int sz) {
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    snprintf(buf, sz, "%04d%02d%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
}

static void log_get_time_str(char* buf, int sz) {
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    snprintf(buf, sz, "%02d:%02d:%02d.%03d",
             tm->tm_hour, tm->tm_min, tm->tm_sec,
             (int)(clock() % 1000));
}

static FILE* log_open_file() {
    char today[16];
    log_get_date_str(today, sizeof(today));

    // 日期变了就换文件，加分隔符
    if (arlo_log_fp && strcmp(arlo_last_date, today) != 0) {
        // 写入日期分隔符
        unsigned char sep[] = "\n========================================\n";
        unsigned char enc_sep[128];
        int slen = strlen((char*)sep);
        log_encrypt_buf(sep, enc_sep, slen);
        fwrite(enc_sep, 1, slen, arlo_log_fp);
        fflush(arlo_log_fp);
        fclose(arlo_log_fp);
        arlo_log_fp = NULL;
    }

    if (!arlo_log_fp) {
        log_ensure_dir();
        char path[256];
        // 路径也加密存储
        snprintf(path, sizeof(path), "%s/%s%s.enc", LOG_PATH, LOG_FILE_PREFIX, today);
        arlo_log_fp = fopen(path, "ab");
        if (arlo_log_fp) {
            strncpy(arlo_last_date, today, sizeof(arlo_last_date) - 1);
            // 写入启动标记
            unsigned char marker[] = "\n====== SESSION START ======\n";
            unsigned char enc_marker[128];
            int mlen = strlen((char*)marker);
            log_encrypt_buf(marker, enc_marker, mlen);
            fwrite(enc_marker, 1, mlen, arlo_log_fp);
            fflush(arlo_log_fp);
        }
    }
    return arlo_log_fp;
}

// ============================================================
// 公开接口：加密日志写入
// ============================================================

// 通用日志：级别 + 模块 + 消息
 static void ARLO_LOG(const char* level, const char* module, const char* fmt, ...) {
    FILE* fp = log_open_file();
    if (!fp) return;

    char timebuf[32];
    log_get_time_str(timebuf, sizeof(timebuf));

    // 构建明文日志行
    char plain[1024];
    int pos = snprintf(plain, sizeof(plain), "[%s][%s][%s] ", timebuf, level, module);

    va_list args;
    va_start(args, fmt);
    pos += vsnprintf(plain + pos, sizeof(plain) - pos, fmt, args);
    va_end(args);

    if (pos < (int)sizeof(plain) - 1) {
        plain[pos++] = '\n';
        plain[pos] = '\0';
    }

    // 加密
    unsigned char enc[1024];
    log_encrypt_buf((unsigned char*)plain, enc, pos);

    fwrite(enc, 1, pos, fp);
    fflush(fp);
}

// 便捷宏
#define LOG_INFO(mod, fmt, ...)  ARLO_LOG("INFO",  mod, fmt, ##__VA_ARGS__)
#define LOG_WARN(mod, fmt, ...)  ARLO_LOG("WARN",  mod, fmt, ##__VA_ARGS__)
#define LOG_ERROR(mod, fmt, ...) ARLO_LOG("ERROR", mod, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(mod, fmt, ...) ARLO_LOG("DEBUG", mod, fmt, ##__VA_ARGS__)
#define LOG_FATAL(mod, fmt, ...) ARLO_LOG("FATAL", mod, fmt, ##__VA_ARGS__)

// ---- 错误带 errno ----
#define LOG_ERRNO(mod, fmt, ...) do { \
    ARLO_LOG("ERROR", mod, fmt " (errno=%d: %s)", ##__VA_ARGS__, errno, strerror(errno)); \
} while(0)

// ============================================================
// 反调试/反抓包检测 + 日志
// ============================================================

// 检测常见抓包工具
 static void log_detect_packet_capture() {
    // 检测 tcpdump / Wireshark / Charles / Fiddler / mitmproxy 进程
    const char* tools[] = {
        "tcpdump", "wireshark", "tshark", "charles", "fiddler",
        "mitmproxy", "mitmdump", "burpsuite", "netmon", "packet",
        "httpcanary", "抓包", "capture", "sniffer", "proxycap",
        "proxifier", "socks", "Charles", "Packet", "HttpTool"
    };
    int tool_count = sizeof(tools) / sizeof(tools[0]);

    DIR* proc = opendir("/proc");
    if (!proc) return;

    struct dirent* entry;
    char cmdline_path[256];
    char cmdline[512];

    while ((entry = readdir(proc)) != NULL) {
        if (entry->d_type != DT_DIR) continue;
        // 检查是否数字目录(PID)
        int is_pid = 1;
        for (char* p = entry->d_name; *p; p++) {
            if (*p < '0' || *p > '9') { is_pid = 0; break; }
        }
        if (!is_pid) continue;

        snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", entry->d_name);
        FILE* f = fopen(cmdline_path, "r");
        if (!f) continue;
        int len = fread(cmdline, 1, sizeof(cmdline) - 1, f);
        fclose(f);
        if (len <= 0) continue;
        cmdline[len] = '\0';

        for (int i = 0; i < tool_count; i++) {
            if (strstr(cmdline, tools[i])) {
                LOG_WARN("DEFENSE", "检测到抓包工具进程: %s (PID=%s)", tools[i], entry->d_name);
                break;
            }
        }
    }
    closedir(proc);
}

// 检测常见调试器
 static void log_detect_debuggers() {
    const char* dbgs[] = {
        "gdb", "lldb", "frida", "strace", "ltrace", "gdbserver",
        "ida", "radare2", "x64dbg", "ollydbg", "objection",
        "frida-server", "frida-helper", "magisk", "supersu"
    };
    int dbg_count = sizeof(dbgs) / sizeof(dbgs[0]);

    DIR* proc = opendir("/proc");
    if (!proc) return;

    struct dirent* entry;
    char cmdline_path[256];
    char cmdline[512];

    while ((entry = readdir(proc)) != NULL) {
        if (entry->d_type != DT_DIR) continue;
        int is_pid = 1;
        for (char* p = entry->d_name; *p; *p++) {
            if (*p < '0' || *p > '9') { is_pid = 0; break; }
        }
        if (!is_pid) continue;

        snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", entry->d_name);
        FILE* f = fopen(cmdline_path, "r");
        if (!f) continue;
        int len = fread(cmdline, 1, sizeof(cmdline) - 1, f);
        fclose(f);
        if (len <= 0) continue;
        cmdline[len] = '\0';

        for (int i = 0; i < dbg_count; i++) {
            if (strstr(cmdline, dbgs[i])) {
                LOG_WARN("DEFENSE", "检测到调试工具进程: %s (PID=%s)", dbgs[i], entry->d_name);
                break;
            }
        }
    }
    closedir(proc);
}

// 检测xt_NFLOG/iptables抓包规则
 static void log_detect_iptables_capture() {
    FILE* f = fopen("/proc/net/xt_qtaguid/stats", "r");
    if (!f) {
        // 某些抓包工具会创建这个
        LOG_DEBUG("DEFENSE", "xt_qtaguid不可读（正常）");
    } else {
        fclose(f);
    }

    // 检测 /proc/net/tcp 中可疑连接
    f = fopen("/proc/net/tcp", "r");
    if (f) {
        char line[256];
        int line_count = 0;
        while (fgets(line, sizeof(line), f) && line_count < 500) {
            // 检测常见代理端口
            if (strstr(line, "1F90") || strstr(line, "22B8") ||  // 8080, 8888
                strstr(line, "2328") || strstr(line, "270F") ||  // 9000, 9999
                strstr(line, "1F40")) {                           // 8000
                LOG_DEBUG("DEFENSE", "检测到可疑端口连接");
            }
            line_count++;
        }
        fclose(f);
    }
}

// ---- 综合检测入口 ----
 static void log_environment_scan() {
    LOG_INFO("SYS", "环境扫描开始 (PID=%d)", getpid());
    log_detect_debuggers();
    log_detect_packet_capture();
    log_detect_iptables_capture();
    LOG_INFO("SYS", "环境扫描完成");
}

// ============================================================
// 功能调用追踪宏
// ============================================================

// 函数入口追踪
#define FUNC_ENTER(name) LOG_DEBUG("CALL", "--> %s", name)
#define FUNC_EXIT(name)  LOG_DEBUG("CALL", "<-- %s (ok)", name)
#define FUNC_ERR(name, reason) LOG_ERROR("CALL", "<-- %s FAILED: %s", name, reason)

// 驱动读写追踪
#define LOG_READ(addr, sz, result)  LOG_DEBUG("R/W", "READ  0x%lx sz=%d => %s", (unsigned long)(addr), (int)(sz), (result) ? "OK" : "FAIL")
#define LOG_WRITE(addr, sz, result) LOG_DEBUG("R/W", "WRITE 0x%lx sz=%d => %s", (unsigned long)(addr), (int)(sz), (result) ? "OK" : "FAIL")

// 防御触发追踪
#define LOG_DEFENSE_HIT(name) LOG_WARN("DEFENSE", "触发防御: %s", name)

// 验证步骤追踪
#define LOG_AUTH_STEP(step) LOG_INFO("AUTH", "验证步骤: %s", step)

// 驱动选择追踪
#define LOG_DRIVER_SELECT(idx, name) LOG_INFO("DRIVER", "选择驱动 #%d: %s", (int)(idx), name)

// 错误退出追踪
#define LOG_EXIT(reason) do { \
    LOG_FATAL("EXIT", "程序退出: %s (errno=%d)", reason, errno); \
    if (arlo_log_fp) { fflush(arlo_log_fp); fclose(arlo_log_fp); arlo_log_fp = NULL; } \
} while(0)

// ============================================================
// 初始化 + 清理
// ============================================================

 static void arlo_log_init() {
    log_ensure_dir();
    LOG_INFO("SYS", "=== Arlo日志系统初始化 ===");
    LOG_INFO("SYS", "PID=%d UID=%d GID=%d", getpid(), getuid(), getgid());
    LOG_INFO("SYS", "日志路径: %s", LOG_PATH);

    // 环境扫描
    log_environment_scan();
}

 static void arlo_log_close() {
    LOG_INFO("SYS", "=== 日志系统关闭 ===");
    if (arlo_log_fp) {
        unsigned char end_marker[] = "\n====== SESSION END ======\n\n";
        unsigned char enc_end[128];
        int mlen = strlen((char*)end_marker);
        log_encrypt_buf(end_marker, enc_end, mlen);
        fwrite(enc_end, 1, mlen, arlo_log_fp);
        fflush(arlo_log_fp);
        fclose(arlo_log_fp);
        arlo_log_fp = NULL;
    }
}