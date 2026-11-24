/* ==================================================================
   xl_https.h —— 进程内 HTTPS 客户端（dlopen libssl/libcrypto，带证书 pin）
   目的：
     1) 彻底去掉 popen("curl …")，不再依赖外部程序 / sh / PATH
     2) 不再有 `-k`，改为校验服务器 SPKI 的 SHA-256 指纹（pin）
     3) 握手/收发全部在本进程内，外部替换 curl、PATH 劫持都失效
   依赖：Encrypt.h 的 sha256_buf；系统库 libssl.so / libcrypto.so（Android 自带）
   ================================================================== */
#ifndef XLV_HTTPS_H
#define XLV_HTTPS_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cstdint>
#include <vector>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>

/* 日志默认关闭：发布版不往 logcat 泄漏 URL / 时间戳 / 请求体。
   排查问题时在 Android.mk 里加 -DXLV_VERBOSE 打开。 */
#if defined(XLV_VERBOSE)
#  if defined(__ANDROID__)
#    include <android/log.h>
#    define XLV_TLS_LOG(...) __android_log_print(ANDROID_LOG_INFO, "XLTLS", __VA_ARGS__)
#  else
#    define XLV_TLS_LOG(...) do { printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while (0)
#  endif
#else
#  define XLV_TLS_LOG(...) do { } while (0)
#endif

/* 目标证书 SubjectPublicKeyInfo 的 SHA-256（Let's Encrypt 续期不变，换了密钥才变） */
#ifndef XLV_PIN_SPKI
#define XLV_PIN_SPKI "b4d333bbe777e54e9289fcbc4879bf9c3b2a4a1b525f60f25e5d3d69e0fc7f0e"
#endif

namespace XLV {
namespace tls {

struct Api {
    void *h_ssl, *h_crypto;
    void *(*TLS_client_method)();
    void *(*SSL_CTX_new)(const void *);
    void (*SSL_CTX_free)(void *);
    void (*SSL_CTX_set_verify)(void *, int, void *);
    void *(*SSL_new)(void *);
    void (*SSL_free)(void *);
    int (*SSL_set_fd)(void *, int);
    long (*SSL_ctrl)(void *, int, long, void *);
    /* Android 系统的 libssl.so 是 BoringSSL：没有 SSL_ctrl，但有这个函数。
       OpenSSL 3 反过来（此符号是宏，dlsym 拿不到）。两条路都要挂。 */
    int (*SSL_set_tlsext_host_name)(void *, const char *);
    int (*SSL_connect)(void *);
    int (*SSL_write)(void *, const void *, int);
    int (*SSL_read)(void *, void *, int);
    void (*SSL_shutdown)(void *);
    void *(*SSL_get1_peer_certificate)(const void *);
    void *(*SSL_get_peer_certificate)(const void *);
    void *(*X509_get_X509_PUBKEY)(void *);
    int (*i2d_X509_PUBKEY)(const void *, unsigned char **);
    void (*X509_free)(void *);
    void (*CRYPTO_free)(void *, const char *, int);
};

static Api &api() {
    static Api a;
    static bool tried = false;
    if (tried) return a;
    tried = true;
    memset(&a, 0, sizeof(a));

    const char *ssl_libs[] = { "/system/lib64/libssl.so", "/system/lib/libssl.so",
                               "libssl.so.3", "libssl.so.1.1", "libssl.so" };
    const char *crypto_libs[] = { "/system/lib64/libcrypto.so", "/system/lib/libcrypto.so",
                                  "libcrypto.so.3", "libcrypto.so.1.1", "libcrypto.so" };

    for (size_t i = 0; i < sizeof(ssl_libs) / sizeof(ssl_libs[0]) && !a.h_ssl; ++i) {
        void *h = dlopen(ssl_libs[i], RTLD_NOW);
        if (!h) continue;
        a.TLS_client_method   = (void *(*)())dlsym(h, "TLS_client_method");
        a.SSL_CTX_new         = (void *(*)(const void *))dlsym(h, "SSL_CTX_new");
        a.SSL_CTX_free        = (void (*)(void *))dlsym(h, "SSL_CTX_free");
        a.SSL_CTX_set_verify  = (void (*)(void *, int, void *))dlsym(h, "SSL_CTX_set_verify");
        a.SSL_new             = (void *(*)(void *))dlsym(h, "SSL_new");
        a.SSL_free            = (void (*)(void *))dlsym(h, "SSL_free");
        a.SSL_set_fd          = (int (*)(void *, int))dlsym(h, "SSL_set_fd");
        a.SSL_ctrl            = (long (*)(void *, int, long, void *))dlsym(h, "SSL_ctrl");
        a.SSL_set_tlsext_host_name = (int (*)(void *, const char *))dlsym(h, "SSL_set_tlsext_host_name");
        a.SSL_connect         = (int (*)(void *))dlsym(h, "SSL_connect");
        a.SSL_write           = (int (*)(void *, const void *, int))dlsym(h, "SSL_write");
        a.SSL_read            = (int (*)(void *, void *, int))dlsym(h, "SSL_read");
        a.SSL_shutdown        = (void (*)(void *))dlsym(h, "SSL_shutdown");
        a.SSL_get1_peer_certificate = (void *(*)(const void *))dlsym(h, "SSL_get1_peer_certificate");
        a.SSL_get_peer_certificate  = (void *(*)(const void *))dlsym(h, "SSL_get_peer_certificate");
        if (a.TLS_client_method && a.SSL_CTX_new && a.SSL_new && a.SSL_connect && a.SSL_read)
            a.h_ssl = h;
        else
            memset(&a, 0, sizeof(a));
    }
    for (size_t i = 0; i < sizeof(crypto_libs) / sizeof(crypto_libs[0]) && !a.h_crypto; ++i) {
        void *h = dlopen(crypto_libs[i], RTLD_NOW);
        if (!h) continue;
        a.X509_get_X509_PUBKEY = (void *(*)(void *))dlsym(h, "X509_get_X509_PUBKEY");
        a.i2d_X509_PUBKEY      = (int (*)(const void *, unsigned char **))dlsym(h, "i2d_X509_PUBKEY");
        a.X509_free            = (void (*)(void *))dlsym(h, "X509_free");
        a.CRYPTO_free          = (void (*)(void *, const char *, int))dlsym(h, "CRYPTO_free");
        if (a.X509_get_X509_PUBKEY && a.i2d_X509_PUBKEY) a.h_crypto = h;
        else { a.X509_get_X509_PUBKEY = NULL; a.i2d_X509_PUBKEY = NULL; }
    }
    if (a.h_ssl && a.h_crypto) XLV_TLS_LOG("libssl/libcrypto 已加载");
    else XLV_TLS_LOG("libssl/libcrypto 加载失败");
    return a;
}

/* TOFU（首次信任）：只有在 pin 来源里显式写了 "tofu" 时才启用。
   用途：服务器在墙外 / 拿不到证书时，先放行一次并把真实指纹打进日志，
   抄进 11054.pin 后立刻恢复严格校验。默认关闭，不影响任何现有行为。 */
static bool g_pin_tofu = false;

static bool ci_has(const std::string &s, const char *needle) {
    size_t n = strlen(needle);
    if (!n || s.size() < n) return false;
    for (size_t i = 0; i + n <= s.size(); ++i) {
        size_t j = 0;
        while (j < n) {
            char a = (char)tolower((unsigned char)s[i + j]);
            char b = (char)tolower((unsigned char)needle[j]);
            if (a != b) break;
            ++j;
        }
        if (j == n) return true;
    }
    return false;
}

/* -------- 允许的 pin 列表：环境变量 > 外部文件 > 编译期常量 --------
   支持一个来源里写多个指纹（换行 / 空格 / 逗号分隔），任意命中即通过。
   多值是为「正式域名证书 + 中转域名证书」「证书轮换过渡期」同时放行。 */
static std::vector<std::string> pin_accepts() {
    std::string src;
    const char *env = getenv("XLV_PIN");
    if (env) src = env;
    if (src.empty()) {
        const char *f = "/storage/emulated/0/Android/" XLV_APPID ".pin";
        FILE *fp = fopen(f, "rb");
        if (fp) {
            char buf[4096] = {0};
            size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
            fclose(fp);
            src.assign(buf, n);
        }
    }
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i <= src.size(); ++i) {
        char c = (i < src.size()) ? src[i] : ' ';
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (hex) cur += (char)tolower((unsigned char)c);
        else {
            if (cur.size() == 64) out.push_back(cur);
            cur.clear();
        }
    }
    if (out.empty()) {
        /* 编译期常量也支持多值（空格 / 逗号 / 换行分隔），
           便于「原站证书 + 中转证书」「证书轮换过渡期」同时放行 */
        std::string d = std::string(XLV_PIN_SPKI);
        std::string cd;
        for (size_t i = 0; i <= d.size(); ++i) {
            char c = (i < d.size()) ? d[i] : ' ';
            bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (hex) cd += (char)tolower((unsigned char)c);
            else { if (cd.size() == 64) out.push_back(cd); cd.clear(); }
        }
        if (out.empty()) out.push_back(std::string(XLV_PIN_SPKI));
    }
    g_pin_tofu = ci_has(src, "tofu");
    return out;
}

static int tcp_connect(const char *host, int port, int timeout_ms) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%d", port);
    if (getaddrinfo(host, portbuf, &hints, &res) != 0 || !res) return -1;

    int fd = -1;
    for (struct addrinfo *p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;

        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int r = connect(fd, p->ai_addr, p->ai_addrlen);
        if (r < 0 && errno == EINPROGRESS) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);
            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            r = select(fd + 1, NULL, &wfds, NULL, &tv);
            if (r > 0) {
                int soerr = 0;
                socklen_t sl = sizeof(soerr);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
                r = (soerr == 0) ? 0 : -1;
            } else {
                r = -1;
            }
        }
        fcntl(fd, F_SETFL, flags);
        if (r == 0) {
            struct timeval tv;
            tv.tv_sec = 12;
            tv.tv_usec = 0;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static std::string hex_lower(const unsigned char *b, int n) {
    static const char *t = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < n; ++i) { s += t[b[i] >> 4]; s += t[b[i] & 15]; }
    return s;
}

/* chunked 解码（服务端万一分块） */
static std::string dechunk(const std::string &in) {
    std::string out;
    size_t p = 0;
    while (p < in.size()) {
        size_t e = in.find("\r\n", p);
        if (e == std::string::npos) break;
        std::string szline = in.substr(p, e - p);
        size_t semi = szline.find(';');
        if (semi != std::string::npos) szline = szline.substr(0, semi);
        long n = strtol(szline.c_str(), NULL, 16);
        if (n <= 0) break;
        p = e + 2;
        if (p + (size_t)n > in.size()) { out += in.substr(p); break; }
        out += in.substr(p, (size_t)n);
        p += (size_t)n + 2;
    }
    return out;
}

/* 返回响应体；失败返回空串并在 err 里给原因 */
static std::string https_post(const std::string &hostport, const std::string &path_query,
                             const std::string &body, std::string &err) {
    Api &a = api();
    if (!a.h_ssl || !a.h_crypto) { err = "libssl/libcrypto 未加载"; return std::string(); }

    std::string host = hostport;
    int port = 443;
    size_t colon = hostport.rfind(':');
    if (colon != std::string::npos) {
        bool allnum = true;
        for (size_t i = colon + 1; i < hostport.size(); ++i)
            if (hostport[i] < '0' || hostport[i] > '9') allnum = false;
        if (allnum && colon + 1 < hostport.size()) {
            port = atoi(hostport.c_str() + colon + 1);
            host = hostport.substr(0, colon);
        }
    }

    int fd = tcp_connect(host.c_str(), port, 8000);
    if (fd < 0) { err = "TCP 连接失败"; return std::string(); }

    const void *meth = a.TLS_client_method();
    void *ctx = a.SSL_CTX_new(meth);
    if (!ctx) { close(fd); err = "SSL_CTX_new 失败"; return std::string(); }
    if (a.SSL_CTX_set_verify) a.SSL_CTX_set_verify(ctx, 0 /* SSL_VERIFY_NONE：本模块自己做 pin */, NULL);

    void *ssl = a.SSL_new(ctx);
    if (!ssl) { a.SSL_CTX_free(ctx); close(fd); err = "SSL_new 失败"; return std::string(); }
    a.SSL_set_fd(ssl, fd);
    std::string out;          /* 提前声明：下面 SNI 失败要 goto done，不能跨过它的初始化 */
    /* ---- SNI（必须在握手前设置，否则服务器回默认站点证书）----
       Android 系统的 libssl.so 是 BoringSSL：没有 SSL_ctrl，只有 SSL_set_tlsext_host_name；
       OpenSSL 3 相反，SSL_set_tlsext_host_name 是宏（dlsym 拿不到），要走 SSL_ctrl(55)。
       两路都试；都不可用就直接失败——不允许"无 SNI 继续握手"。 */
    {
        bool sni_ok = false;
        if (a.SSL_set_tlsext_host_name)
            sni_ok = (a.SSL_set_tlsext_host_name(ssl, host.c_str()) == 1);
        if (!sni_ok && a.SSL_ctrl)
            sni_ok = (a.SSL_ctrl(ssl, 55 /* SSL_CTRL_SET_TLSEXT_HOSTNAME */,
                                 0 /* TLSEXT_NAMETYPE_host_name */,
                                 (void *)host.c_str()) > 0);
        if (!sni_ok) {
            err = "无法设置 SNI（libssl 缺少 SSL_set_tlsext_host_name/SSL_ctrl）";
            XLV_TLS_LOG("SNI 设置失败 host=%s", host.c_str());
            goto done;
        }
        XLV_TLS_LOG("SNI = %s", host.c_str());
    }

    if (a.SSL_connect(ssl) != 1) {
        err = "TLS 握手失败";
        goto done;
    }

    /* ---- 证书 pin：比对 SubjectPublicKeyInfo 的 SHA-256 ---- */
    {
        void *cert = NULL;
        if (a.SSL_get1_peer_certificate) cert = a.SSL_get1_peer_certificate(ssl);
        else if (a.SSL_get_peer_certificate) cert = a.SSL_get_peer_certificate(ssl);
        if (!cert) { err = "取不到服务器证书"; goto done; }

        void *spki = a.X509_get_X509_PUBKEY(cert);
        unsigned char *der = NULL;
        int derlen = spki ? a.i2d_X509_PUBKEY(spki, &der) : 0;
        if (derlen <= 0 || !der) {
            if (a.X509_free) a.X509_free(cert);
            err = "证书公钥解析失败";
            goto done;
        }
        unsigned char digest[32];
        sha256_buf(der, (size_t)derlen, digest);
        std::string got = hex_lower(digest, 32);
        if (a.CRYPTO_free) a.CRYPTO_free(der, __FILE__, __LINE__);
        if (a.X509_free) a.X509_free(cert);

        /* 第三方托管域名（Vercel / Cloudflare / Render）证书 90 天轮换、
           SPKI 不固定，pin 无法长期维持；这类域名跳过 pin。
           防伪造由响应里的 Ed25519 签名保证（无签名私钥就无法伪造通过）。 */
        bool skip_pin = false;
        {
            static const char *sfx[] = { ".vercel.app", ".workers.dev",
                                         ".pages.dev", ".onrender.com" };
            for (int si = 0; si < 4 && !skip_pin; ++si) {
                size_t sl = strlen(sfx[si]);
                if (host.size() > sl && host.compare(host.size() - sl, sl, sfx[si]) == 0)
                    skip_pin = true;
            }
        }
        std::vector<std::string> want = pin_accepts();
        bool ok = skip_pin;
        if (skip_pin) XLV_TLS_LOG("跳过 pin（托管域名，靠响应签名）host=%s", host.c_str());
        for (size_t i = 0; i < want.size(); ++i)
            if (want[i] == got) { ok = true; break; }
        if (!ok) {
            if (g_pin_tofu) {
                /* 只在 pin 来源里显式写了 tofu 时才走这里：放行一次，把真指纹打进日志 */
                XLV_TLS_LOG("[TOFU] 首次信任放行 got=%s ← 把这 64 位抄进 "
                            XLV_APPID ".pin 后即恢复严格校验", got.c_str());
                ok = true;
            } else {
                /* UI 一行放不下 64 位，只显示前 16 位，完整值进日志 */
                err = "证书指纹不匹配（可能被中间人）：got=" + got.substr(0, 16) + "…";
                XLV_TLS_LOG("PIN MISMATCH got=%s want[0]=%s n=%d",
                            got.c_str(), want.empty() ? "" : want[0].c_str(), (int)want.size());
                goto done;
            }
        }
        XLV_TLS_LOG("证书 pin 校验通过");
    }

    /* ---- 发送请求 ---- */
    {
        char head[512];
        int hn = snprintf(head, sizeof(head),
                          "POST /%s HTTP/1.1\r\n"
                          "Host: %s\r\n"
                          "User-Agent: XLClient/1.1\r\n"
                          "Accept: */*\r\n"
                          "Content-Type: application/x-www-form-urlencoded\r\n"
                          "Content-Length: %d\r\n"
                          "Connection: close\r\n\r\n",
                          path_query.c_str(), host.c_str(), (int)body.size());
        std::string req(head, hn);
        req += body;

        size_t sent = 0;
        while (sent < req.size()) {
            int n = a.SSL_write(ssl, req.data() + sent, (int)(req.size() - sent));
            if (n <= 0) { err = "TLS 发送失败"; goto done; }
            sent += (size_t)n;
        }
    }

    /* ---- 收响应 ---- */
    {
        std::string raw;
        char buf[4096];
        for (;;) {
            int n = a.SSL_read(ssl, buf, (int)sizeof(buf));
            if (n > 0) { raw.append(buf, n); continue; }
            break;
        }
        if (raw.empty()) { err = "空响应"; goto done; }

        size_t hdr_end = raw.find("\r\n\r\n");
        std::string head = (hdr_end == std::string::npos) ? raw : raw.substr(0, hdr_end);
        std::string payload = (hdr_end == std::string::npos) ? std::string() : raw.substr(hdr_end + 4);

        if (head.find("HTTP/1.1 200") == std::string::npos &&
            head.find("HTTP/1.0 200") == std::string::npos) {
            size_t e1 = head.find("\r\n");
            err = "HTTP 状态非 200：" + (e1 == std::string::npos ? head : head.substr(0, e1));
            goto done;
        }
        if (head.find("Transfer-Encoding: chunked") != std::string::npos ||
            head.find("transfer-encoding: chunked") != std::string::npos)
            payload = dechunk(payload);
        out = payload;
    }

done:
    if (a.SSL_shutdown) a.SSL_shutdown(ssl);
    a.SSL_free(ssl);
    a.SSL_CTX_free(ctx);
    close(fd);
    return out;
}

} /* namespace tls */
} /* namespace XLV */

#endif /* XLV_HTTPS_H */