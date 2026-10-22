/* =============================================================================
 *  CT 卡密本地伪造 —— 原生 ELF 可执行文件（AIDE / NDK 可编译）
 *
 *  原理：目标 App 的卡密校验走明文 HTTP
 *          POST http://<IP>/api/v2/kami/verify
 *          GET  http://<IP>/api/v2/official_params?pkg=..&ver=..
 *        用 iptables 把该 App 的 80 端口流量重定向到本机 8080，
 *        本地按路径返回"验证成功"JSON。
 *
 *  特点：纯 C 实现，不依赖 busybox / awk / shell 脚本。
 *        内含 MD5 无关逻辑，仅需 libc。
 *
 *  用法：
 *    ./ct_bypass            交互菜单
 *    ./ct_bypass 1|start    开启
 *    ./ct_bypass 2|stop     关闭
 *    ./ct_bypass 3|status   状态
 *    ./ct_bypass 4|log      查看请求日志
 *    ./ct_bypass serve      仅前台跑服务（调试）
 *
 *  需要 root（iptables）。
 * ========================================================================== */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ======================== 配置 ======================== */
#define PORT        8080
#define PKG         "com.ly.origin"
#define TARGET_IP1  "64.90.17.177"      /* 卡密验证服务器 */
#define TARGET_IP2  "111.32.210.0/24"   /* shell/云注入服务器网段 */
#define LOGFILE     "/data/local/tmp/ct_req.log"
#define PIDFILE     "/data/local/tmp/.ct_bypass.pid"
#define UID_FALLBACK 10458

/* ======================== 日志 ======================== */
static void logline(const char *fmt, ...) {
    FILE *f = fopen(LOGFILE, "a");
    if (!f) return;
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    fprintf(f, "=== %02d:%02d:%02d ===\n", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

/* ======================== 响应模板 ======================== */

/* 卡密验证成功（字段给足，多余的会被忽略） */
static void build_kami_resp(const char *kami, char *out, size_t cap) {
    snprintf(out, cap,
        "{\"code\":200,\"message\":\"\\u9a8c\\u8bc1\\u6210\\u529f\",\"data\":{"
        "\"kami\":\"%s\","
        "\"token\":\"ct_local_forged_token_0000000000000000\","
        "\"expire\":\"2099-01-01 00:00:00\","
        "\"expired_at\":\"2099-01-01 00:00:00\","
        "\"vip\":1,\"vip_level\":9,"
        "\"type\":\"year\",\"ktype\":\"year\",\"kmtype\":\"year\","
        "\"remaining\":99999,\"remain\":99999,\"days\":99999,"
        "\"status\":1,\"success\":true,\"valid\":true,\"is_vip\":true,"
        "\"device_limit\":99,\"bind\":1,\"uid\":1000001,\"id\":1000001,"
        "\"username\":\"local\",\"nickname\":\"local\""
        "},\"time\":4070908800}",
        kami ? kami : "local");
}

/* 公告/参数：空公告 + 版本正常 + 不强制更新 */
static const char *RESP_OFFICIAL =
    "{\"code\":200,\"message\":\"success\",\"data\":{"
    "\"notice\":\"\",\"tip\":\"\",\"date\":\"2099-01-01\","
    "\"params\":\"\",\"map\":\"\",\"version\":\"1.8.6\","
    "\"force_update\":0,\"update_url\":\"\",\"disable\":0,\"switch\":1}}";

/* 其它 /api 接口：通用成功 */
static const char *RESP_GENERIC =
    "{\"code\":200,\"message\":\"success\",\"data\":{"
    "\"vip\":1,\"expire\":\"2099-01-01 00:00:00\",\"status\":1,\"success\":true}}";

/* ======================== 小工具 ======================== */
static int starts_with(const char *s, const char *pre) {
    return s && pre && strncmp(s, pre, strlen(pre)) == 0;
}
static int contains(const char *hay, const char *needle) {
    return hay && needle && strstr(hay, needle) != NULL;
}

/* 从 body 里取 "kami":"xxx" */
static void extract_json_field(const char *body, const char *key, char *out, size_t cap) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    out[0] = 0;
    if (!body) return;
    const char *p = strstr(body, pat);
    if (!p) return;
    p += strlen(pat);
    while (*p && (*p == ' ' || *p == '\t')) p++;
    if (*p != ':') return;
    p++;
    while (*p && (*p == ' ' || *p == '\t')) p++;
    if (*p == '"') {
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i + 1 < cap) {
            if (*p == '\\' && p[1]) p++;   /* 简单反转义 */
            out[i++] = *p++;
        }
        out[i] = 0;
    } else {
        size_t i = 0;
        while (*p && *p != ',' && *p != '}' && i + 1 < cap) out[i++] = *p++;
        out[i] = 0;
    }
}

/* ======================== HTTP 服务 ======================== */

static void send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) break;
        sent += (size_t)n;
    }
}

static void reply_json(int fd, const char *json) {
    char head[512];
    size_t blen = strlen(json);
    int n = snprintf(head, sizeof(head),
        "HTTP/1.1 200 OK\r\n"
        "Server: nginx\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n", blen);
    send_all(fd, head, (size_t)n);
    send_all(fd, json, blen);
}

static void handle_conn(int fd) {
    static char req[65536];
    size_t n = 0;
    size_t clen = 0;
    int have_hdr = 0;
    char firstline[512] = {0};
    char method[16] = {0}, path[2048] = {0};

    /* 读请求 */
    while (n < sizeof(req) - 1) {
        ssize_t r = recv(fd, req + n, sizeof(req) - 1 - n, 0);
        if (r <= 0) break;
        n += (size_t)r;
        req[n] = 0;
        char *hdr_end = strstr(req, "\r\n\r\n");
        if (hdr_end) {
            if (!have_hdr) {
                have_hdr = 1;
                sscanf(req, "%15s %2047s", method, path);
                if (!firstline[0]) sscanf(req, "%511[^\r\n]", firstline);
                char *c = strstr(req, "Content-Length:");
                if (!c) c = strstr(req, "content-length:");
                if (c) clen = (size_t)strtoul(c + 15, NULL, 10);
            }
            if (n >= (size_t)(hdr_end + 4 - req) + clen) break;
        }
    }

    const char *body = "";
    char *hdr_end = strstr(req, "\r\n\r\n");
    if (hdr_end) {
        body = hdr_end + 4;
        if (*body == 0) body = "";
    }

    char kami[256] = {0};
    char did[256] = {0};
    extract_json_field(body, "kami", kami, sizeof(kami));
    extract_json_field(body, "did", did, sizeof(did));

    char logmsg[1024];
    snprintf(logmsg, sizeof(logmsg), "REQ: %s\nKAMI: %s\nDID: %s\nBODY: %s",
             firstline[0] ? firstline : "(?)", kami, did,
             (body && strlen(body) < 400) ? body : "(large)");
    logline("%s", logmsg);
    printf("[REQ] %s  kami=%s\n", path[0] ? path : firstline, kami);
    fflush(stdout);

    /* 按路径分流 */
    if (contains(path, "/kami/verify")) {
        char resp[2048];
        build_kami_resp(kami[0] ? kami : "local", resp, sizeof(resp));
        reply_json(fd, resp);
    } else if (contains(path, "/official_params")) {
        reply_json(fd, RESP_OFFICIAL);
    } else if (contains(path, "/api/")) {
        reply_json(fd, RESP_GENERIC);
    } else {
        reply_json(fd, RESP_GENERIC);
    }
    close(fd);
}

static volatile int g_run = 1;
static void on_sig(int s) { (void)s; g_run = 0; }

static int serve_forever(void) {
    int sfd, on = 1;
    struct sockaddr_in addr;
    struct sigaction sa;

    sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); return 1; }
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);
    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(sfd, 32) < 0) { perror("listen"); return 1; }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sig;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    logline("SERVER listening on 0.0.0.0:%d (pkg=%s)", PORT, PKG);
    printf("[OK] 假服务端监听 0.0.0.0:%d\n", PORT);
    fflush(stdout);

    while (g_run) {
        int cfd = accept(sfd, NULL, NULL);
        if (cfd < 0) { if (errno == EINTR) continue; break; }
        pid_t pid = fork();
        if (pid == 0) {
            close(sfd);
            handle_conn(cfd);
            _exit(0);
        }
        close(cfd);
        while (waitpid(-1, NULL, WNOHANG) > 0) { }
    }
    close(sfd);
    return 0;
}

/* ======================== 系统操作 ======================== */
static int sh(const char *cmd) { return system(cmd); }

static int get_app_uid(void) {
    char buf[512] = {0}, pid[64] = {0};
    FILE *f = popen("pidof " PKG " 2>/dev/null", "r");
    if (f) {
        if (fgets(buf, sizeof(buf), f)) sscanf(buf, "%63s", pid);
        pclose(f);
    }
    if (pid[0]) {
        char path[96];
        struct stat st;
        snprintf(path, sizeof(path), "/proc/%s", pid);
        if (stat(path, &st) == 0 && st.st_uid > 0) return (int)st.st_uid;
    }
    f = popen("dumpsys package " PKG " 2>/dev/null | grep -m1 userId=", "r");
    if (f) {
        buf[0] = 0;
        if (fgets(buf, sizeof(buf), f)) {
            char *u = strstr(buf, "userId=");
            if (u) {
                int v = atoi(u + 7);
                pclose(f);
                if (v > 0) return v;
            }
        }
        pclose(f);
    }
    return UID_FALLBACK;
}

static void rules_clear(void) {
    int n = 0;
    while (n++ < 40) {
        FILE *f = popen("iptables -t nat -L OUTPUT -n --line-numbers 2>/dev/null "
                        "| grep 'redir ports 8080' | head -1 | awk '{print $1}'", "r");
        char ln[32] = {0};
        if (!f) break;
        if (!fgets(ln, sizeof(ln), f)) { pclose(f); break; }
        pclose(f);
        int L = atoi(ln);
        if (L <= 0) break;
        char cmd[160];
        snprintf(cmd, sizeof(cmd), "iptables -t nat -D OUTPUT %d 2>/dev/null", L);
        sh(cmd);
    }
}

static void rules_add(int uid) {
    char cmd[512];
    /* 1) 按 App uid 劫持所有 80 端口流量 */
    snprintf(cmd, sizeof(cmd),
        "iptables -t nat -A OUTPUT -p tcp --dport 80 -m owner --uid-owner %d "
        "-j REDIRECT --to-ports %d", uid, PORT);
    sh(cmd);
    /* 2) 按目标 IP 兜底 */
    snprintf(cmd, sizeof(cmd),
        "iptables -t nat -A OUTPUT -p tcp -d " TARGET_IP1 " --dport 80 "
        "-j REDIRECT --to-ports %d", PORT);
    sh(cmd);
    snprintf(cmd, sizeof(cmd),
        "iptables -t nat -A OUTPUT -p tcp -d " TARGET_IP2 " --dport 80 "
        "-j REDIRECT --to-ports %d", PORT);
    sh(cmd);
}

static int pid_alive(pid_t p) { return (p > 0) && (kill(p, 0) == 0); }

static pid_t read_pid(void) {
    char b[32] = {0};
    int fd = open(PIDFILE, O_RDONLY);
    if (fd < 0) return 0;
    ssize_t r = read(fd, b, sizeof(b) - 1);
    close(fd);
    if (r <= 0) return 0;
    return (pid_t)atoi(b);
}

/* ======================== 命令 ======================== */

static int do_start(void) {
    pid_t pid = read_pid();
    if (pid_alive(pid)) {
        printf("[i] 已在运行 (pid %d)\n", (int)pid);
        return 0;
    }
    rules_clear();
    unlink(LOGFILE);

    pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid > 0) {
        sleep(1);
        int uid = get_app_uid();
        rules_add(uid);
        printf("[OK] 假服务端: %s\n", pid_alive(pid) ? "运行中" : "启动失败");
        printf("[OK] 劫持规则: uid=%d 的 80 端口 → %d\n", uid, PORT);
        printf("[OK] 目标IP: %s / %s\n", TARGET_IP1, TARGET_IP2);
        printf("[i]  日志: %s\n", LOGFILE);
        printf("[i]  现在在 App 里随便填卡密点登录\n");
        return 0;
    }

    /* 子进程：守护化 */
    setsid();
    {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); }
    }
    int fd = open(PIDFILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char pbuf[32];
        snprintf(pbuf, sizeof(pbuf), "%d", (int)getpid());
        if (write(fd, pbuf, strlen(pbuf)) < 0) { /* ignore */ }
        close(fd);
    }
    serve_forever();
    unlink(PIDFILE);
    _exit(0);
}

static int do_stop(void) {
    pid_t pid = read_pid();
    if (pid_alive(pid)) {
        kill(pid, SIGTERM);
        usleep(400000);
        if (pid_alive(pid)) kill(pid, SIGKILL);
    }
    sh("pkill -f 'nc -lk -p 8080' 2>/dev/null");
    rules_clear();
    unlink(PIDFILE);
    printf("[OK] 已停止：服务端结束，劫持规则已清除\n");
    return 0;
}

static int do_status(void) {
    pid_t pid = read_pid();
    printf("  假服务端 : %s", pid_alive(pid) ? "运行中" : "未运行");
    if (pid_alive(pid)) printf(" (pid %d)", (int)pid);
    printf("\n  监听端口 : %d\n", PORT);
    printf("  目标 App : %s (uid %d)\n", PKG, get_app_uid());
    {
        FILE *f = popen("iptables -t nat -L OUTPUT -n 2>/dev/null | grep -c 'redir ports 8080'", "r");
        char b[32] = "0";
        if (f) { if (fgets(b, sizeof(b), f)) b[strcspn(b, "\n")] = 0; pclose(f); }
        printf("  劫持规则 : %s 条\n", b);
    }
    {
        FILE *f = popen("pidof " PKG " 2>/dev/null", "r");
        char b[64] = {0};
        if (f) { if (fgets(b, sizeof(b), f)) b[strcspn(b, "\n")] = 0; pclose(f); }
        printf("  App 进程 : %s\n", b[0] ? b : "(未运行)");
    }
    printf("  最近请求 :\n");
    {
        FILE *f = popen("tail -12 " LOGFILE " 2>/dev/null", "r");
        char line[512];
        int any = 0;
        if (f) {
            while (fgets(line, sizeof(line), f)) { printf("    %s", line); any = 1; }
            pclose(f);
        }
        if (!any) printf("    (无)\n");
    }
    return 0;
}

static int do_log(void) {
    FILE *f = fopen(LOGFILE, "r");
    if (!f) { printf("(无日志)\n"); return 0; }
    char line[1024];
    while (fgets(line, sizeof(line), f)) fputs(line, stdout);
    fclose(f);
    return 0;
}

static int do_selftest(void) {
    char resp[2048];
    printf("[*] 自检\n");
    build_kami_resp("SELFTEST", resp, sizeof(resp));
    printf("    verify 响应长度: %zu\n", strlen(resp));
    printf("    含 code:200      : %s\n", strstr(resp, "\"code\":200") ? "是" : "否");
    printf("    含 expire 2099   : %s\n", strstr(resp, "2099-01-01") ? "是" : "否");
    printf("    official 响应长度: %zu\n", strlen(RESP_OFFICIAL));
    printf("    目标 App uid     : %d\n", get_app_uid());
    printf("    ==> 自检完成\n");
    return 0;
}

/* ======================== 主程序 ======================== */
int main(int argc, char **argv) {
    if (argc > 1) {
        const char *a = argv[1];
        if (!strcmp(a, "1") || !strcmp(a, "start"))    return do_start();
        if (!strcmp(a, "2") || !strcmp(a, "stop"))     return do_stop();
        if (!strcmp(a, "3") || !strcmp(a, "status"))   return do_status();
        if (!strcmp(a, "4") || !strcmp(a, "selftest")) return do_selftest();
        if (!strcmp(a, "5") || !strcmp(a, "log"))      return do_log();
        if (!strcmp(a, "serve"))                       return serve_forever();
        printf("用法: %s [1|start] [2|stop] [3|status] [4|selftest] [5|log] [serve]\n", argv[0]);
        return 0;
    }

    for (;;) {
        char line[64];
        printf("==========================================\n");
        printf("   CT 卡密本地伪造 (原生 ELF)\n");
        printf("   目标: %s  端口: %d\n", PKG, PORT);
        printf("   需要 root\n");
        printf("------------------------------------------\n");
        printf("   1) 开启\n");
        printf("   2) 关闭\n");
        printf("   3) 状态\n");
        printf("   4) 自检\n");
        printf("   5) 日志\n");
        printf("   0) 退出\n");
        printf("==========================================\n");
        printf("请输入选项: ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        int c = atoi(line);
        if (c == 1)       do_start();
        else if (c == 2)  do_stop();
        else if (c == 3)  do_status();
        else if (c == 4)  do_selftest();
        else if (c == 5)  do_log();
        else if (c == 0)  break;
        else printf("[!] 无效选项\n");
        printf("\n");
    }
    return 0;
}
