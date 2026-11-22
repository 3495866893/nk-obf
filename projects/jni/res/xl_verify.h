/* ==================================================================
   XL云验证 客户端模块（自包含，header-only）
   协议：SM4-CBC(动态IV) + HMAC-SM3
     data = Base64( 16B IV + SM4-CBC( 8B随机 + 4B秒级时间戳[大端] + 业务参数 ) + 32B HMAC-SM3(key,IV||密文) )
     业务参数 = kami=..&user=..&markcode=..&t=..&sign=..&value=..
     sign     = MD5("kami=" + 卡密 + "&user=" + 账号 + "&markcode=" + 设备码 + "&t=" + 时间戳 + "&" + APPKEY)
     check    = MD5(服务端time + APPKEY + value)      本地复算校验
   依赖：同目录 Encrypt.h（Base64/UrlEncode）、gm.h（SM4/SM3/HMAC）、cJSON
   用法：
     XLV::Init();                 // 启动时一次：读已存卡密，有则后台验证
     XLV::Tick();                 // 每帧调用
     if (XLV::Verified()) {...} else XLV::ShowWindow(&running);
   ================================================================== */
#ifndef XLV_VERIFY_H
#define XLV_VERIFY_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dlfcn.h>
#include <cctype>
#include <sys/utsname.h>
#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

/* 日志默认关闭：发布版不往 logcat 泄漏卡密 / 请求路径 / 响应长度。
   排查问题时在 Android.mk 里加 -DXLV_VERBOSE 打开。 */
#if defined(XLV_VERBOSE)
#  if defined(__ANDROID__)
#    include <android/log.h>
#    define XLV_TAG "XLVERIFY"
#    define XLV_LOGI(...) __android_log_print(ANDROID_LOG_INFO, XLV_TAG, __VA_ARGS__)
#  else
#    define XLV_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while (0)
#  endif
#else
#  define XLV_LOGI(...) do { } while (0)
#endif

#include "cJSON.h"
#include "cJSON.c"
#include "Encrypt.h"
#include "gm.h"

/* ImGui 登录窗：只在能探测到 imgui.h 时编译（Android.mk 已含 include/ImGui） */
#if defined(__has_include)
#  if __has_include("imgui.h")
#    include "imgui.h"
#    define XLV_HAS_IMGUI 1
#  endif
#endif

/* ---------------- 对接参数（与已验证工程保持一致） ---------------- */
#ifndef XLV_APPID
#define XLV_APPID    "11054"
#endif
#ifndef XLV_APPKEY
#define XLV_APPKEY   "y4sH2w11imYIx1K4"
#endif
#ifndef XLV_AESKEY
#define XLV_AESKEY   "mN4bV7cX1zL9kJ2hG5fD8sA0qW3enknb"   /* AES 路线备用，当前不用 */
#endif
#ifndef XLV_GMKEY
#define XLV_GMKEY    "tYHhrtawMQIdR5Cx"                    /* 后台 SM4 密钥（16 字节） */
#endif
#ifndef XLV_HOST
#define XLV_HOST     "xl-relay.vercel.app"                  /* 中转（带 Ed25519 签名）；改回 "yz.dujiuyue.cn" 即直连 */
#endif

/* 加固 A 开关：
     1 = 响应必须带 Ed25519 签名，验签不过直接失败（走自建签名中转时必须 1）
     0 = 只做旧的 MD5 check 校验（直连官方后台时只能是 0，因为官方不发 sig）
   默认跟随编译开关：定义了 XLV_RELAY 才强制验签，否则 0，
   这样「直连官方」和「走中转」两种打包方式都不会踩到“全部登录失败”。 */
#ifndef XLV_REQUIRE_SIG
#  ifdef XLV_RELAY
#    define XLV_REQUIRE_SIG 1
#  else
#    define XLV_REQUIRE_SIG 0
#  endif
#endif

/* ============================================================
   加固第 2/3 条：进程内 TLS + 证书 pin（不再 popen curl，不再 -k）
   依赖 XLV_APPID（pin 文件路径）与 sha256_buf（Encrypt.h），故必须在此处 include
   ============================================================ */
#include "xl_https.h"
#if XLV_REQUIRE_SIG
#include "xl_ed25519.h"   /* 加固 A：Ed25519 验签（服务端私钥签名，客户端只存公钥） */
#endif

namespace XLV {

/* ============================ 内部工具 ============================ */
namespace detail {

/* ---- MD5（RFC1321 精简实现） ---- */
struct Md5Ctx { uint32_t st[4]; uint64_t len; unsigned char buf[64]; };

static inline uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static void md5_block(Md5Ctx *c, const unsigned char *p) {
    static const uint32_t K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391 };
    static const int S[64] = {
        7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
        5, 9,14,20,5, 9,14,20,5, 9,14,20,5, 9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
        6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21 };
    uint32_t M[16];
    for (int i = 0; i < 16; ++i)
        M[i] = (uint32_t)p[i*4] | ((uint32_t)p[i*4+1] << 8) |
               ((uint32_t)p[i*4+2] << 16) | ((uint32_t)p[i*4+3] << 24);
    uint32_t a = c->st[0], b = c->st[1], cc = c->st[2], d = c->st[3];
    for (int i = 0; i < 64; ++i) {
        uint32_t f; int g;
        if (i < 16)      { f = (b & cc) | (~b & d);        g = i; }
        else if (i < 32) { f = (d & b) | (~d & cc);        g = (5*i + 1) % 16; }
        else if (i < 48) { f = b ^ cc ^ d;                 g = (3*i + 5) % 16; }
        else             { f = cc ^ (b | ~d);              g = (7*i) % 16; }
        uint32_t tmp = d;
        d = cc; cc = b;
        b = b + rol(a + f + K[i] + M[g], S[i]);
        a = tmp;
    }
    c->st[0] += a; c->st[1] += b; c->st[2] += cc; c->st[3] += d;
}

static void md5_init(Md5Ctx *c) {
    c->st[0] = 0x67452301; c->st[1] = 0xefcdab89;
    c->st[2] = 0x98badcfe; c->st[3] = 0x10325476;
    c->len = 0;
    memset(c->buf, 0, sizeof(c->buf));
}

static void md5_update(Md5Ctx *c, const void *data, size_t n) {
    const unsigned char *p = (const unsigned char *)data;
    size_t used = (size_t)(c->len & 63);
    c->len += n;
    if (used) {
        size_t need = 64 - used;
        if (n < need) { memcpy(c->buf + used, p, n); return; }
        memcpy(c->buf + used, p, need);
        md5_block(c, c->buf);
        p += need; n -= need;
    }
    while (n >= 64) { md5_block(c, p); p += 64; n -= 64; }
    if (n) memcpy(c->buf, p, n);
}

static void md5_final(Md5Ctx *c, unsigned char out[16]) {
    uint64_t bits = c->len << 3;
    unsigned char pad[72];
    size_t used = (size_t)(c->len & 63);
    size_t padlen = (used < 56) ? (56 - used) : (120 - used);
    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    for (int i = 0; i < 8; ++i) pad[padlen + i] = (unsigned char)(bits >> (8 * i));
    md5_update(c, pad, padlen + 8);
    for (int i = 0; i < 4; ++i) {
        out[i*4+0] = (unsigned char)(c->st[i]);
        out[i*4+1] = (unsigned char)(c->st[i] >> 8);
        out[i*4+2] = (unsigned char)(c->st[i] >> 16);
        out[i*4+3] = (unsigned char)(c->st[i] >> 24);
    }
}

static std::string md5_hex(const std::string &s) {
    Md5Ctx c; unsigned char d[16]; char hex[33];
    md5_init(&c); md5_update(&c, s.data(), s.size()); md5_final(&c, d);
    static const char *tb = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) { hex[i*2] = tb[d[i] >> 4]; hex[i*2+1] = tb[d[i] & 15]; }
    hex[32] = 0;
    return std::string(hex);
}

/* ---- 文件读写 ---- */
static bool read_text(const char *path, std::string &out) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    char buf[512]; size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[n] = 0;
    out.assign(buf);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' ||
                            out.back() == ' ' || out.back() == '\t' || out.back() == '\0'))
        out.pop_back();
    return !out.empty();
}

static bool write_text(const char *path, const std::string &s) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return false;
    fwrite(s.data(), 1, s.size(), fp);
    fclose(fp);
    return true;
}

static void ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return;
    mkdir(path, 0777);
}

/* ---- shell 单引号转义 ---- */
static std::string shq(const std::string &s) {
    std::string r = "'";
    for (char ch : s) { if (ch == '\'') r += "'\\''"; else r += ch; }
    r += "'";
    return r;
}

static std::string run_read(const std::string &cmd) {
    std::string out;
    FILE *fp = popen(cmd.c_str(), "r");
    if (!fp) return out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) out.append(buf, n);
    pclose(fp);
    return out;
}

/* ---- HTTPS POST：进程内 TLS（dlopen libssl/libcrypto）+ SPKI 证书 pin ----
   加固第 2/3 条：彻底去掉 popen("curl -k ...")。
   原实现的两个问题：
     ① -k 关掉了证书校验 → 任意中间人可替换响应；
     ② 依赖外部 /system/bin/curl，LD_PRELOAD / PATH 劫持 popen 即可换掉响应。
   现在：TLS 在自己进程内完成，且强制比对服务器公钥 SPKI-SHA256。 */
static std::string http_post(const std::string &path_query, const std::string &body,
                             std::string *err_out = NULL) {
    std::string err;
    std::string r = tls::https_post(XLV_HOST, path_query, body, err);
    /* 只清理换行/空白，不做任何“看起来像 JSON 就放过”的宽松判断 */
    std::string t;
    t.reserve(r.size());
    for (size_t i = 0; i < r.size(); ++i) {
        char ch = r[i];
        if (ch != '\n' && ch != '\r' && ch != ' ' && ch != '\t') t += ch;
    }
    if (t.empty() && err_out) *err_out = err.empty() ? std::string("空响应") : err;
    if (!err.empty()) XLV_LOGI("https_post: %s", err.c_str());
    return t;
}

/* ---- JSON 取值容错 ---- */
static int jint(cJSON *o, const char *k, int def) {
    cJSON *v = cJSON_GetObjectItem(o, k);
    if (!v) return def;
    if (v->type == cJSON_Number) return (int)v->valuedouble;
    if (v->type == cJSON_String && v->valuestring) return atoi(v->valuestring);
    return def;
}
static long long jint64(cJSON *o, const char *k, long long def) {
    cJSON *v = cJSON_GetObjectItem(o, k);
    if (!v) return def;
    if (v->type == cJSON_Number) return (long long)v->valuedouble;
    if (v->type == cJSON_String && v->valuestring) return atoll(v->valuestring);
    return def;
}
static std::string jstr(cJSON *o, const char *k, const char *def) {
    cJSON *v = cJSON_GetObjectItem(o, k);
    if (v && v->type == cJSON_String && v->valuestring) return std::string(v->valuestring);
    if (v && v->type == cJSON_Number) {
        char b[64]; snprintf(b, sizeof(b), "%lld", (long long)v->valuedouble); return std::string(b);
    }
    return std::string(def);
}

/* ---- 加固 A：响应 Ed25519 验签（fail-closed）----
   签名内容为固定顺序的 ASCII 串：
       kami=<kami>&value=<value>&vip=<vip>&time=<服务器时间>
   value 是本次请求里的随机值（服务端从请求里取），等于把签名绑死在这一次请求上，
   重放旧签名、改 vip、换卡密都会失效。
   原来的 check=md5(time+APPKEY+value) 用的是对称密钥，APPKEY 在客户端里，
   攻击者能自己算；这条只有服务端私钥能签，客户端只有公钥。 */
#if XLV_REQUIRE_SIG
static bool verify_response_sig(cJSON *root, cJSON *msgobj, const std::string &km_req,
                                long long st, long long vip, const std::string &value,
                                std::string &err) {
    std::string sig_b64 = jstr(root, "sig", "");
    if (sig_b64.empty()) { err = "响应缺少签名（sig）"; return false; }
    std::string kami = km_req;
    if (msgobj && msgobj->type == cJSON_Object) {
        std::string s = jstr(msgobj, "kami", "");
        if (!s.empty()) kami = s;
    }
    std::string msg = "kami=" + kami + "&value=" + value + "&vip=" +
                      std::to_string(vip) + "&time=" + std::to_string(st);

    int slen = 0;
    unsigned char *sig = Base64Decode(sig_b64.c_str(), &slen);
    if (!sig || slen != 64) {
        if (sig) delete[] sig;
        err = "签名长度不是 64 字节";
        return false;
    }
    unsigned char pub[32];
    if (!ed25519::pub_from_hex(XLV_ED25519_PUB, pub)) {
        delete[] sig;
        err = "内置公钥格式错误";
        return false;
    }
    bool ok = ed25519::verify_raw((const unsigned char *)msg.c_str(), msg.size(), sig, pub);
    delete[] sig;
    if (!ok) { err = "签名校验失败（响应被篡改或不是官方服务端）"; return false; }
    return true;
}
#endif /* XLV_REQUIRE_SIG */

/* 秒级时间戳归一化（兼容毫秒/微秒返回） */
static long long norm_ts(long long t) {
    if (t > 100000000000000LL) return t / 1000000LL;
    if (t > 100000000000LL) return t / 1000LL;
    return t;
}
static bool looks_b64(const std::string &s) {
    if (s.size() < 16) return false;
    size_t n = 0;
    for (char ch : s) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '+' || ch == '/' || ch == '=') ++n;
    }
    return n * 100 >= s.size() * 95;
}

static const char *imei_path() { return "/storage/emulated/0/Android/" XLV_APPID ".imei"; }

/* 设备码：随机10位 → md5，持久化，重装/重启后不变 */
static std::string device_code() {
    std::string code;
    ensure_dir("/storage/emulated/0/Android");
    if (read_text(imei_path(), code) && code.size() == 32) return code;
    static const char tb[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char raw[11];
    srand((unsigned)(time(NULL) ^ getpid()));
    for (int i = 0; i < 10; ++i) raw[i] = tb[rand() % (int)(sizeof(tb) - 1)];
    raw[10] = 0;
    code = md5_hex(std::string(raw));
    write_text(imei_path(), code);
    return code;
}

/* ==================================================================
   剪贴板读取（供「粘贴」按钮用）
   原理：Android 10+ 禁止非前台进程直接读系统剪贴板，
        但这台机器的输入法（搜狗）把剪贴板历史落在 SQLite 库里：
        /data/data/<ime>/databases/clipboard_db  (表 CLIPBOARD_ITEM.CONTENT)
        root 可读 → 取最新一条卡密格式的内容。
   依赖：dlopen 系统库 /system/lib64/libsqlite.so（符号全为 GLOBAL）
   ================================================================== */
struct SqliteApi {
    void *handle;
    int (*open_v2)(const char *, void **, int, const char *);
    int (*prepare_v2)(void *, const char *, int, void **, const char **);
    int (*step)(void *);
    const unsigned char *(*column_text)(void *, int);
    int (*finalize)(void *);
    int (*close)(void *);
};

static SqliteApi &sqlite_api() {
    static SqliteApi a;
    static bool tried = false;
    if (tried) return a;
    tried = true;
    memset(&a, 0, sizeof(a));
    const char *libs[] = { "/system/lib64/libsqlite.so", "/system/lib/libsqlite.so",
                           "libsqlite.so", "libsqlite3.so", "libsqlite3.so.0" };
    for (size_t i = 0; i < sizeof(libs) / sizeof(libs[0]); ++i) {
        void *h = dlopen(libs[i], RTLD_NOW);
        if (!h) continue;
        a.open_v2    = (int (*)(const char *, void **, int, const char *))dlsym(h, "sqlite3_open_v2");
        a.prepare_v2 = (int (*)(void *, const char *, int, void **, const char **))dlsym(h, "sqlite3_prepare_v2");
        a.step       = (int (*)(void *))dlsym(h, "sqlite3_step");
        a.column_text= (const unsigned char *(*)(void *, int))dlsym(h, "sqlite3_column_text");
        a.finalize   = (int (*)(void *))dlsym(h, "sqlite3_finalize");
        a.close      = (int (*)(void *))dlsym(h, "sqlite3_close");
        if (a.open_v2 && a.prepare_v2 && a.step && a.column_text && a.finalize && a.close) {
            a.handle = h;
            XLV_LOGI("libsqlite 已加载：%s", libs[i]);
            return a;
        }
        memset(&a, 0, sizeof(a));
    }
    XLV_LOGI("libsqlite 加载失败");
    return a;
}

static bool sqlite_rows(const std::string &db, const char *sql,
                        std::vector<std::string> &out, int maxrows) {
    SqliteApi &a = sqlite_api();
    if (!a.open_v2) return false;
    void *dh = NULL;
    /* SQLITE_OPEN_READONLY = 0x00000001 */
    if (a.open_v2(db.c_str(), &dh, 0x00000001, NULL) != 0 || !dh) {
        if (dh) a.close(dh);
        return false;
    }
    void *st = NULL;
    bool ok = false;
    if (a.prepare_v2(dh, sql, -1, &st, NULL) == 0 && st) {
        while (a.step(st) == 100 /* SQLITE_ROW */ && (int)out.size() < maxrows) {
            const unsigned char *t = a.column_text(st, 0);
            if (t) out.push_back(std::string((const char *)t));
        }
        a.finalize(st);
        ok = true;
    }
    a.close(dh);
    return ok;
}

static bool ends_with(const std::string &s, const char *suf) {
    size_t n = strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

/* 找输入法剪贴板库：环境变量 > 已知路径 > root 下全局搜索 */
static std::string find_clip_db() {
    const char *env = getenv("XLV_CLIP_DB");
    if (env && access(env, R_OK) == 0) return std::string(env);
    const char *known = "/data/data/com.sohu.inputmethod.sogouoem/databases/clipboard_db";
    if (access(known, R_OK) == 0) return std::string(known);

    std::string r = run_read("su -c 'ls /data/data/*/databases/*clip* 2>/dev/null' 2>/dev/null");
    size_t p = 0;
    while (p < r.size()) {
        size_t e = r.find('\n', p);
        if (e == std::string::npos) e = r.size();
        std::string line = r.substr(p, e - p);
        p = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        if (ends_with(line, "-journal") || ends_with(line, "-wal") || ends_with(line, "-shm")) continue;
        return line;
    }
    return std::string();
}

/* 非 root 场景：用 su 复制一份可读副本（含 WAL 副档）再查 */
static std::string copy_db_for_read(const std::string &db) {
    static const char *dst = "/data/local/tmp/xlv_clip.db";
    /* 路径全部用 shq() 单引号转义，再整体包给 su -c，杜绝命令注入 */
    std::string inner = "cp " + shq(db) + " " + shq(dst) + " 2>/dev/null; "
                        "cp " + shq(db + "-wal") + " " + shq(std::string(dst) + "-wal") + " 2>/dev/null; "
                        "chmod 644 " + shq(dst) + " 2>/dev/null";
    run_read("su -c " + shq(inner) + " 2>/dev/null");
    if (access(dst, R_OK) == 0) return std::string(dst);
    return std::string();
}

/* 卡密格式清洗：去掉所有空白，长度 4~64，仅字母数字/-/_ */
static bool card_like(const std::string &s, std::string &out) {
    std::string t;
    for (char c : s) {
        if (c == '\r' || c == '\n' || c == '\t' || c == ' ') continue;
        t += c;
    }
    if (t.size() < 4 || t.size() > 64) return false;
    for (char c : t) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') || c == '-' || c == '_')) return false;
    }
    out = t;
    return true;
}

/* 粘贴：返回卡密，失败时 why 里带原因 */
static std::string paste_text(std::string &why) {
    std::string db = find_clip_db();
    if (!db.empty()) {
        static const char *sql =
            "SELECT CONTENT FROM CLIPBOARD_ITEM ORDER BY TIME DESC, _id DESC LIMIT 20";
        std::vector<std::string> rows;
        bool ok = sqlite_rows(db, sql, rows, 20);
        if (!ok) {
            std::string cp = copy_db_for_read(db);
            if (!cp.empty()) ok = sqlite_rows(cp, sql, rows, 20);
        }
        if (ok && !rows.empty()) {
            std::string best;
            for (size_t i = 0; i < rows.size(); ++i)
                if (card_like(rows[i], best)) { why.clear(); return best; }
            why = "剪贴板最近没有卡密格式的内容";
        } else {
            why = "剪贴板库读取失败（需要 root）";
        }
    } else {
        why = "没找到输入法剪贴板库";
    }

    /* 备用：文本文件 */
    const char *files[] = { "/storage/emulated/0/Download/kami.txt",
                            "/storage/emulated/0/Android/" XLV_APPID ".paste",
                            "/data/local/tmp/kami.txt" };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        std::string t;
        if (!read_text(files[i], t)) continue;
        std::string c;
        if (card_like(t, c)) { why.clear(); return c; }
        why = std::string("文件内容不是卡密格式：") + files[i];
    }
    return std::string();
}

/* 解析响应：只接受 SM4 密文，且 GmUnwrap 内已强制校验 HMAC-SM3。
   ---- 加固第 4 条（fail-closed）----
   以前这里会先试 "明文 JSON"：攻击者不需要任何密钥，只要让服务器回
   {"code":200,"msg":{...}} 就能登录成功。现在明文一律拒绝，只认
   “带正确 HMAC 标签的 SM4 密文”，伪造响应必须先持有后台密钥。 */
static cJSON *parse_response(const std::string &body, std::string *plain_out = NULL) {
    if (body.empty()) return NULL;
    if (plain_out) plain_out->clear();
    /* 去掉所有空白，密文不应含空白 */
    std::string t;
    t.reserve(body.size());
    for (size_t i = 0; i < body.size(); ++i) {
        char ch = body[i];
        if (ch != '\n' && ch != '\r' && ch != ' ' && ch != '\t') t += ch;
    }
    if (!looks_b64(t)) {
        XLV_LOGI("拒绝响应：不是 Base64 密文（明文响应已封禁）len=%d", (int)t.size());
        return NULL;
    }
    char *pt = GmUnwrap(t.c_str(), XLV_GMKEY);   /* 标签校验失败会返回 NULL */
    if (!pt) {
        XLV_LOGI("拒绝响应：HMAC-SM3 标签校验失败或数据损坏");
        return NULL;
    }
    if (plain_out) *plain_out = pt;
    cJSON *root = cJSON_Parse(pt);
    delete[] pt;
    return root;
}

/* 一次完整登录。user 传空串即可（服务端要求 &user= 存在，值可为空） */
/* 把 https_post 的原始错误翻成用户能看懂的一句话。
   5xx / 520~524 都是官方服务器或 CDN 侧的问题，不是本机网络，
   直白展示原始响应反而会让人以为是自己网坏了。 */
static std::string http_err_text(const std::string &neterr) {
    /* 对外只暴露两类范围：网络问题 / 服务器问题。
       具体是 DNS、TCP、TLS 还是证书 pin 一概不提。 */
    if (neterr.empty()) return std::string("网络问题");
    static const char *srv[] = {" 500 ", " 502 ", " 503 ", " 504 ",
                               " 520 ", " 521 ", " 522 ", " 523 ", " 524 "};
    for (const char *sv : srv)
        if (neterr.find(sv) != std::string::npos)
            return std::string("服务器问题");
    if (neterr.find("HTTP 状态非 200") != std::string::npos)
        return std::string("服务器问题");
    return std::string("网络问题");
}

static bool do_login(const std::string &km, const std::string &user,
                     std::string &msg_out, std::string &expiry_out,
                     long long *vip_out = nullptr) {
    if (km.empty()) { msg_out = "卡密不能为空"; return false; }
    std::string imei = device_code();
    if (imei.size() != 32) { msg_out = "401"; return false; }

    int t = (int)time(NULL);
    std::string sign_src = "kami=" + km + "&user=" + user + "&markcode=" + imei +
                           "&t=" + std::to_string(t) + "&" + XLV_APPKEY;
    std::string sign = md5_hex(sign_src);

    char valbuf[64];
    snprintf(valbuf, sizeof(valbuf), "%d%u", 1000 + (int)(time(NULL) % 99000), (unsigned)t);
    std::string value(valbuf);

    std::string plain = "kami=" + km + "&user=" + user + "&markcode=" + imei +
                        "&t=" + std::to_string(t) + "&sign=" + sign + "&value=" + value;

    char *enc = GmWrap(plain.c_str(), XLV_GMKEY);
    if (!enc) { msg_out = "401"; return false; }
    std::string b64(enc);
    delete[] enc;

    char *urlenc = UrlEncode(b64.c_str());
    std::string body = std::string("data=") + (urlenc ? urlenc : b64.c_str());
    if (urlenc) delete[] urlenc;

    std::string path = std::string("api.php?api=kmlogon&app=") + XLV_APPID;
    XLV_LOGI("POST %s t=%d", path.c_str(), t);
    std::string neterr;
    std::string resp = http_post(path, body, &neterr);
    if (resp.empty()) {
        msg_out = detail::http_err_text(neterr);
        return false;
    }
    XLV_LOGI("resp=%s", resp.size() > 300 ? (resp.substr(0, 300) + "...").c_str() : resp.c_str());

    std::string plain_dbg;
    cJSON *root = parse_response(resp, &plain_dbg);
    if (!root) { msg_out = "401"; return false; }
    XLV_LOGI("plain=%s", plain_dbg.c_str());

    int code = jint(root, "code", 0);
    /* msg 可能是字符串（错误提示）或对象（成功时含 kami/vip） */
    cJSON *msgobj = cJSON_GetObjectItem(root, "msg");
    std::string smsg = (msgobj && msgobj->type == cJSON_String && msgobj->valuestring)
                           ? std::string(msgobj->valuestring)
                           : std::string("验证失败");
    long long st = norm_ts(jint64(root, "time", 0));
    std::string check = jstr(root, "check", "");

    if (code != 200) {
        cJSON_Delete(root);
        (void)smsg;
        msg_out = std::to_string(code);
        return false;
    }

    /* ---- 加固第 4 条：时间戳必须存在且新鲜（fail-closed）----
       以前是 if (st > 0 && ...) —— 响应里干脆不带 time 就整段跳过，
       攻击者回一个没有 time 字段的包就能免掉重放窗口检查。 */
    long long now = (long long)time(NULL);
    if (st <= 0) {
        cJSON_Delete(root);
        msg_out = "401";
        return false;
    }
    /* 后台设 20 秒，本地放宽到 ±30 秒 */
    if (st - now > 30 || st - now < -30) {
        cJSON_Delete(root);
        msg_out = "401";
        return false;
    }
    /* ---- 加固第 4 条：check 必须存在且 32 位十六进制（fail-closed）----
       以前是 if (!check.empty()) —— 不带 check 就跳过校验。 */
    if (check.size() != 32) {
        cJSON_Delete(root);
        msg_out = "401";
        return false;
    }
    {
        std::string local_check = md5_hex(std::to_string(st) + XLV_APPKEY + value);
        std::string cc = check;
        for (size_t i = 0; i < cc.size(); ++i) cc[i] = (char)tolower((unsigned char)cc[i]);
        if (local_check != cc) {
            cJSON_Delete(root);
            msg_out = "401";
            return false;
        }
    }

    /* 到期时间：vip / endtime / expire 任一，可能嵌在 msg 对象里 */
    long long vip = 0;
    const char *vkeys[] = { "vip", "endtime", "expire", "expiretime" };
    for (int i = 0; i < 4; ++i) {
        long long v = norm_ts(jint64(root, vkeys[i], 0));
        if (v <= 0 && msgobj && msgobj->type == cJSON_Object)
            v = norm_ts(jint64(msgobj, vkeys[i], 0));
        if (v > 0) { vip = v; break; }
    }
    if (vip > 0) {
        time_t vt = (time_t)vip;
        struct tm tmv;
        localtime_r(&vt, &tmv);
        char b[128];
        snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
        expiry_out = b;
    } else {
        expiry_out = "永久/未返回";
    }
    if (vip_out) *vip_out = vip;

    /* ---- 加固 A：Ed25519 验签（fail-closed）----
       check 用的是对称 APPKEY，攻击者拿到 APPKEY 就能自己造；这一条用服务端私钥签，
       设备上只有公钥，Frida 改内存也造不出签名。 */
#if XLV_REQUIRE_SIG
    {
        std::string serr;
        if (!verify_response_sig(root, msgobj, km, st, vip, value, serr)) {
            cJSON_Delete(root);
            (void)serr;
            msg_out = "401";
            XLV_LOGI("签名校验失败：%s", serr.c_str());
            return false;
        }
        XLV_LOGI("Ed25519 验签通过（vip=%lld）", vip);
    }
#endif

    cJSON_Delete(root);

    /* 不落盘、不内置：卡密只存在本次运行内存里 */
    msg_out = "登录成功";
    XLV_LOGI("login OK, expiry=%s", expiry_out.c_str());
    return true;
}

/* 解绑（换设备前用） */
static bool do_unbind(const std::string &km, std::string &msg_out) {
    std::string imei = device_code();
    int t = (int)time(NULL);
    std::string sign_src = "kami=" + km + "&user=&markcode=" + imei +
                           "&t=" + std::to_string(t) + "&" + XLV_APPKEY;
    std::string sign = md5_hex(sign_src);
    std::string plain = "kami=" + km + "&user=&markcode=" + imei +
                        "&t=" + std::to_string(t) + "&sign=" + sign;
    char *enc = GmWrap(plain.c_str(), XLV_GMKEY);
    if (!enc) { msg_out = "401"; return false; }
    char *urlenc = UrlEncode(enc);
    std::string body = std::string("data=") + (urlenc ? urlenc : enc);
    if (urlenc) delete[] urlenc;
    delete[] enc;

    std::string path = std::string("api.php?api=kmunmachine&app=") + XLV_APPID;
    std::string neterr;
    std::string resp = http_post(path, body, &neterr);
    if (resp.empty()) { msg_out = http_err_text(neterr); return false; }
    cJSON *root = parse_response(resp);
    if (!root) { msg_out = "401"; return false; }
    int code = jint(root, "code", 0);
    /* 同样脱敏：成功给固定文案，失败只给数字码 */
    msg_out = (code == 200) ? std::string("解绑完成") : std::to_string(code);
    cJSON_Delete(root);
    return code == 200;
}

} /* namespace detail */

/* ============================ 公共状态机 ============================ */
enum State { StNeedInput = 0, StLogging = 1, StOK = 2, StFail = 3 };

static std::atomic<int> g_state(StNeedInput);
static std::atomic<bool> g_inited(false);
static std::mutex g_mtx;
static std::string g_msg = "请输入卡密";
static std::string g_expiry;
static std::string g_km;
static std::atomic<long long> g_vip(0);      /* 到期时间戳，0=永久 */
static std::atomic<long long> g_ok_at(0);    /* 登录成功的时间点，用于展示几分钟后放行 */
static bool g_dev_btn = false;               /* 开发者设备：在登录界面左上角显示「直接进入」按钮 */
/* 等级：0=绿(正常) 1=黄(即将到期) 2=红(快到期或已过期)
   阀值：>=30 天绿，7~29 天黄，<7 天红 */
static int level_of(long long vip) {
    if (vip <= 0) return 0;
    long long days = (vip - (long long)time(NULL)) / 86400;
    if (days >= 30) return 0;
    if (days >= 7)  return 1;
    return 2;
}
static int get_level() { return level_of(g_vip.load()); }
/* 是否永久（上游未返回到期时间） */
static bool is_permanent() { return g_vip.load() <= 0; }
/* 剩余天数；只在非永久时有效，已过期返回负数 */
static int get_remain_days() {
    long long v = g_vip.load();
    if (v <= 0) return 0;
    return (int)((v - (long long)time(NULL)) / 86400);
}

static void set_msg(const std::string &s) {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_msg = s;
}

static std::string get_msg() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_msg;
}

static std::string get_expiry() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_expiry;
}

static std::string get_km() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_km;
}

static void start_login(const std::string &km, const std::string &user) {
    if (g_state.load() == StLogging || km.empty()) return;
    g_state.store(StLogging);
    set_msg("正在验证…");
    std::string k = km, u = user;
    std::thread th([k, u]() {
        std::string msg, exp;
        long long vip = 0;
        bool ok = detail::do_login(k, u, msg, exp, &vip);
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            g_msg = msg;
            g_expiry = exp;
            if (ok) { g_km = k; g_vip.store(vip); }
        }
        if (ok) g_ok_at.store((long long)time(NULL));
        g_state.store(ok ? StOK : StFail);
    });
    th.detach();
}

/* 启动时调用一次：不读任何本地卡密，全部要用户当次粘贴/输入 */
/* ----------------开发者设备白名单（设备指纹）----------------
   目标：只有本人设备能跳过卡密界面。代码里只存哈希，不存任何明文指纹。

   两层通道（命中任一即通过）：
     [弱通道] 纯属性组合，app 进程可读，不需 root、不弹窗、零延迟
     [强通道] su 读硬件 serial，唯一性★★★，首次需 KernelSU 授权一次

   为何需要 su 通道（实测结论）：
     ro.serialno / ro.boot.serialno  → SELinux serialno_prop，untrusted_app 读到空串
     /sys/devices/soc0/serial_number → vendor_sysfs_soc，untrusted_app 直接 Permission denied
   只有经过 su 切到 root 域才能读。

   弱通道风险（已知）：指纹含固件版本与构建时间，
   同型号 + 同固件版本的其他设备会撞上；且 OTA 升级后会失效。
   要取消它：编译时加 -DXLV_DEV_WEAK=0，只保留 su 强通道。

   安全取舍：所有通道都失败时直接判“不是开发设备”——宁可误拒，不误放。
   说明：本地白名单跳过的只是“输入卡密”这一步，不替代 Ed25519 那层。 */
static const char *XLV_DEV_SALT = "xlr!dev#2026-11-07$8f3a1c9e";

/* 允许的指纹表（命中任一即通过）。全是哈希，无法反推设备身份。 */
static const char *XLV_DEV_FPS[] = {
    "4c8634d30848d1b8165d147ce029fc35",   /* H1  serial        = c4e189d8   */
    "8b9f30d16724ea7433d777cce6006203",   /* H2  soc0 serial   = 3303115224 */
    "96780533042dd5a8473f03d067fb4e2c",   /* H3  纯属性兜底通道              */
    nullptr
};

#ifndef XLV_DEV_WEAK
#  define XLV_DEV_WEAK 0        /* 纯属性兜底（不安全，同型号同固件会撞），默认关 */
#endif

/* 方案 B：设备令牌文件。文件内容只有本人知道，代码里只存哈希。
   好处：不依赖 root、不依赖属性（OTA 不失效）、同型号用户拿不到就过不去。
   令牌丢了就把原文丢回这个路径即可（文件可备份、可迁移到新手机）。 */
#ifndef XLV_DEV_KEY
#  define XLV_DEV_KEY 1
#endif
static const char *XLV_DEV_KEYFILE  = "/storage/emulated/0/Android/.xlv_dev";
static const char *XLV_DEV_KEY_HASH = "550568dd00eb74de82bea7a72bb56f23";
#ifndef XLV_DEV_SU_TIMEOUT_MS
#  define XLV_DEV_SU_TIMEOUT_MS 3000   /* su 等授权上限，超时就杀，避免 UI 卡住 */
#endif

/* ---------------- 工具（纯 POSIX，任何平台都能编译与实测）---------------- */
static std::string xlv_trim(std::string s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

/* 读小文件（≤ 256 字节）并裁掉首尾空白 */
static std::string xlv_readfile(const char *path) {
    int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return std::string();
    char buf[256] = { 0 };
    ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);
    if (n <= 0) return std::string();
    return xlv_trim(std::string(buf, (size_t)n));
}

#if defined(__ANDROID__)
static std::string xlv_prop(const char *k) {
    char buf[PROP_VALUE_MAX] = { 0 };
    __system_property_get(k, buf);
    return std::string(buf);
}

static void xlv_reap(pid_t pid, int ms) {
    for (int i = 0; i < ms / 10; ++i) {
        int st = 0;
        pid_t r = ::waitpid(pid, &st, WNOHANG);
        if (r == pid || r < 0) return;
        ::usleep(10000);
    }
    ::kill(pid, SIGKILL);
    for (int i = 0; i < 20; ++i) {
        int st = 0;
        if (::waitpid(pid, &st, WNOHANG) == pid) return;
        ::usleep(10000);
    }
    XLV_LOGI("dev check: su 回收超时，交给 init 收尸");
}

/* 走 su 读一个值（fork + poll 超时 + 非阻塞回收，不会把 UI 卡死） */
static std::string xlv_su(const char *arg) {
    int fds[2];
    if (::pipe(fds) != 0) return std::string();
    pid_t pid = ::fork();
    if (pid < 0) { ::close(fds[0]); ::close(fds[1]); return std::string(); }
    if (pid == 0) {
        ::close(fds[0]);
        ::dup2(fds[1], STDOUT_FILENO);
        ::close(fds[1]);
        ::execl("/system/bin/su", "su", "-c", arg, (char *)nullptr);
        _exit(127);
    }
    ::close(fds[1]);
    struct pollfd pfd;
    pfd.fd = fds[0];
    pfd.events = POLLIN;
    pfd.revents = 0;
    std::string out;
    int pr = ::poll(&pfd, 1, XLV_DEV_SU_TIMEOUT_MS);
    if (pr > 0) {
        char buf[256] = { 0 };
        ssize_t n = ::read(fds[0], buf, sizeof(buf) - 1);
        if (n > 0) out.assign(buf, (size_t)n);
    }
    ::close(fds[0]);
    if (pr <= 0) {
        ::kill(pid, SIGKILL);
        xlv_reap(pid, 200);
        XLV_LOGI("dev check: su 超时，放弃");
        return std::string();
    }
    xlv_reap(pid, 500);
    return xlv_trim(out);
}

#else
static std::string xlv_prop(const char *) { return std::string(); }
static std::string xlv_su(const char *) { return std::string(); }
#endif

/* 方案 B：设备令牌文件校验（命中即放行，不捷属性、不捳 root） */
#if XLV_DEV_KEY
static bool dev_key_ok() {
    std::string c = xlv_readfile(XLV_DEV_KEYFILE);
    if (c.size() < 16) return false;
    std::string h = detail::md5_hex(detail::md5_hex(c) + XLV_DEV_SALT);
    bool ok = (h == XLV_DEV_KEY_HASH);
    XLV_LOGI("dev check: token len=%d ok=%d", (int)c.size(), (int)ok);
    return ok;
}
#endif

/* 用给定组合算指纹并查表 */
static bool xlv_fp_lookup(const std::string &comp) {
    std::string fp = detail::md5_hex(detail::md5_hex(comp) + XLV_DEV_SALT);
    for (int i = 0; XLV_DEV_FPS[i]; ++i)
        if (fp == XLV_DEV_FPS[i]) return true;
    XLV_LOGI("dev check: len=%d fp=%s hit=0", (int)comp.size(), fp.c_str());
    return false;
}

static bool is_dev_device() {
    static int cached = -1;
    if (cached >= 0) return cached == 1;
    cached = 0;

#if XLV_DEV_KEY
    /* ------ 第 0 层：设备令牌文件（免 root、不捷属性、OTA 不失效）------ */
    if (dev_key_ok()) { cached = 1; return true; }
#endif

#if XLV_DEV_WEAK
    /* ------ 第 1 层：纯属性兜底（不安全，默认关）------ */
    {
        std::string m = xlv_prop("ro.product.model");
        std::string b = xlv_prop("ro.product.brand");
        std::string d = xlv_prop("ro.product.device");
        std::string v = xlv_prop("ro.build.version.sdk");
        if (!m.empty() && !b.empty() && !d.empty() && !v.empty()) {
            std::string comp = m + "|" + b + "|" + d + "|" + v + "|" +
                               xlv_prop("ro.build.display.id") + "|" +
                               xlv_prop("ro.build.date.utc") + "|" +
                               xlv_prop("ro.vendor.build.date.utc");
            if (xlv_fp_lookup(comp)) { cached = 1; return true; }
        } else {
            XLV_LOGI("dev check: 属性不可读，跳过弱通道");
        }
    }
#endif

    /* ------ 第 2 层：su 一次读全（不依赖 app 域可读性）------
       关键：5 个字段全部交给 su 读。否则一旦某个属性在 app 域被限制成空串，
       拼出来的串会残缺，哈希永远对不上。一次 su 输出两行，行内用 | 分隔。 */
    static const char *XLV_SU_CMD =
        "M=$(getprop ro.product.model);B=$(getprop ro.product.brand);"
        "D=$(getprop ro.product.device);V=$(getprop ro.build.version.sdk);"
        "printf '%s|%s|%s|%s|%s\\n' \"$M\" \"$B\" \"$D\" \"$V\" \"$(getprop ro.serialno)\";"
        "printf '%s|%s|%s|%s|%s\\n' \"$M\" \"$B\" \"$D\" \"$V\" \"$(cat /sys/devices/soc0/serial_number)\"";

    std::string c = xlv_su(XLV_SU_CMD);
    size_t pos = 0;
    while (pos < c.size()) {
        size_t q = c.find('\n', pos);
        if (q == std::string::npos) q = c.size();
        std::string line = c.substr(pos, q - pos);
        if (!line.empty() && xlv_fp_lookup(line)) { cached = 1; return true; }
        pos = q + 1;
    }

    return false;
}

static void Init() {
    if (g_inited.exchange(true)) return;
    detail::ensure_dir("/storage/emulated/0/Android");
    /* 开发者设备：只点亮左上角按钮，不自动放行，登录界面照常弹出 */
    g_dev_btn = is_dev_device();
    if (g_dev_btn) XLV_LOGI("dev device: 显示直接进入按钮");

    g_state.store(StNeedInput);
    set_msg("点【粘贴】取卡密，或手动输入");
}

static void Tick() { /* 预留：当前无需主线程轮询，状态由原子变量传递 */ }

static bool Verified() { return g_state.load() == StOK; }
static bool Busy()     { return g_state.load() == StLogging; }
static bool Failed()   { return g_state.load() == StFail; }
static int  StateCode(){ return g_state.load(); }
static std::string Message() { return get_msg(); }
static std::string Expiry()  { return get_expiry(); }

/* 手动登录（UI 按钮调用） */
static void Login(const std::string &km, const std::string &user) {
    if (km.empty()) { set_msg("卡密不能为空"); return; }
    start_login(km, user);
}

/* 粘贴：从剪贴板历史取一条卡密格式的内容（成功返回 true 并写入 out） */
static bool Paste(std::string &out, std::string &why) {
    std::string t = detail::paste_text(why);
    if (t.empty()) {
        set_msg(std::string("粘贴失败：") + why);
        return false;
    }
    out = t;
    set_msg("已粘贴卡密");
    return true;
}

/* 退出登录：只清内存 */
static void Logout() {
    { std::lock_guard<std::mutex> lk(g_mtx); g_km.clear(); g_expiry.clear(); g_msg = "已退出登录，请重新粘贴卡密"; }
    g_state.store(StNeedInput);
}

/* 解绑当前设备（同步，~1 秒，建议在未进入游戏时点） */
static void Unbind() {
    std::string km = get_km();
    if (km.empty()) { set_msg("没有可解绑的卡密"); return; }
    std::string msg;
    bool ok = detail::do_unbind(km, msg);
    set_msg(msg);
    if (ok) Logout();
}

/* ---------------- ImGui 登录窗（在未通过验证时每帧调用） ---------------- */
#ifdef XLV_HAS_IMGUI
/* 等级颜色：绿=正常 黄=即将到期 红=快到期或已过期 */
static ImVec4 level_color(int lv) {
    if (lv == 0) return ImVec4(0.35f, 0.95f, 0.45f, 1.0f);   /* 绿 */
    if (lv == 1) return ImVec4(1.00f, 0.85f, 0.25f, 1.0f);   /* 黄 */
    return ImVec4(1.00f, 0.35f, 0.35f, 1.0f);               /* 红 */
}

/* 登录成功后的等级面板：展示 3 秒再放行 */
static void ShowSuccessPanel() {
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_Always);
    ImGui::Begin("XL云验证", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoResize);
    const int lv = get_level();
    const ImVec4 c = level_color(lv);
    const char *tag = (lv == 0) ? "正常" : (lv == 1) ? "即将到期" : "剩余不足";
    ImGui::TextColored(c, "● %s", get_msg().c_str());
    ImGui::Separator();
    if (is_permanent()) {
        ImGui::TextColored(c, "到期：永久（%s）", get_expiry().c_str());
    } else {
        const int rd = get_remain_days();
        if (rd < 0) ImGui::TextColored(c, "到期：%s（已过期）", get_expiry().c_str());
        else        ImGui::TextColored(c, "到期：%s（%s，剩 %d 天）", get_expiry().c_str(), tag, rd);
    }
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "正在进入…");
    ImGui::End();
}

static bool ShowWindow(bool *running) {
    if (g_state.load() == StOK) {
        long long t0 = g_ok_at.load();
        if (t0 <= 0) return true;                                   /* 开发者按钮放行（g_ok_at=0） */
        if ((long long)time(NULL) - t0 >= 3) return true;           /* 3 秒后放行 */
        ShowSuccessPanel();
        return false;
    }

    /* 开发者设备：屏幕左上角浮动按钮，点一下才放行 */
    if (g_dev_btn) {
        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.80f);
        ImGui::Begin("##xldev", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
        if (ImGui::Button("直接进入", ImVec2(0, 0))) {
            g_state.store(StOK);
            g_ok_at.store(0);
            set_msg("登录成功");
            XLV_LOGI("dev device: 按钮放行");
            ImGui::End();
            return true;
        }
        ImGui::End();
    }

    /* 输入框不预置任何卡密（不内置） */
    static char kmbuf[64] = "";
    static char userbuf[64] = "";

    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_Appearing);
    ImGui::Begin("XL云验证 - 卡密登录", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "卡密登录");
    ImGui::Separator();

    const bool busy = Busy();
    ImGui::BeginDisabled(busy);
    ImGui::SetNextItemWidth(300);
    ImGui::InputTextWithHint("##km", "点【粘贴】或手动输入", kmbuf, sizeof(kmbuf));
    ImGui::SetNextItemWidth(300);
    ImGui::InputTextWithHint("##user", "用户账号（可空）", userbuf, sizeof(userbuf));
    ImGui::EndDisabled();

    if (busy) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "正在验证…");
    } else if (Failed()) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", get_msg().c_str());
    } else {
        ImGui::TextUnformatted(get_msg().c_str());
    }

    ImGui::Separator();
    /* 颜色图例：绿=正常 黄=即将到期 红=剩余不足 */
    ImGui::TextColored(level_color(0), "●"); ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.62f, 0.62f, 0.62f, 1.0f), "正常"); ImGui::SameLine();
    ImGui::TextColored(level_color(1), "●"); ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.62f, 0.62f, 0.62f, 1.0f), "即将到期"); ImGui::SameLine();
    ImGui::TextColored(level_color(2), "●"); ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.62f, 0.62f, 0.62f, 1.0f), "剩余不足");
    ImGui::Separator();

    /* 第一行：粘贴 / 登录 */
    if (busy) ImGui::BeginDisabled(true);
    if (ImGui::Button("粘贴", ImVec2(140, 0))) {
        std::string t, why;
        if (Paste(t, why)) snprintf(kmbuf, sizeof(kmbuf), "%s", t.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("登录", ImVec2(140, 0))) Login(kmbuf, userbuf);
    if (busy) ImGui::EndDisabled();

    /* 第二行：解绑 / 退出 */
    if (ImGui::Button("解绑设备", ImVec2(140, 0))) Unbind();
    ImGui::SameLine();
    if (ImGui::Button("退出程序", ImVec2(140, 0))) { if (running) *running = false; }

    ImGui::End();
    return false;
}
#endif /* XLV_HAS_IMGUI */

} /* namespace XLV */

#endif /* XLV_VERIFY_H */