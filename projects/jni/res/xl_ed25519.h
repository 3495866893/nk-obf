#ifndef XLV_ED25519_H
#define XLV_ED25519_H
/* ==================================================================
   Ed25519 验签（加固 A：把 check 从"对称 MD5"升级成"服务端私钥签名"）

   问题：原来的 check = md5(time + APPKEY + value)，APPKEY 在客户端里，
         攻击者只要拿到 APPKEY 就能自己算 check，伪造响应。
   现在：服务端用 Ed25519 私钥签 (kami,value,vip,time)，客户端只内置公钥，
         验签不过就失败。客户端里没有任何能用来"造签名"的东西，
         Frida 改内存也造不出签名（私钥不在设备上）。

   兼容两条接口（谁有走谁）：
     - BoringSSL（Android 系统 libssl/libcrypto）：ED25519_verify(msg,mlen,sig64,pub32)
     - OpenSSL 3：EVP_PKEY_new_raw_public_key(NID_ED25519=1087) + EVP_DigestVerify
   依赖：Encrypt.h 的 hex/base64 之外不依赖别的；sha256 不用。
   ================================================================== */
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <dlfcn.h>

#ifndef XLV_ED25519_PUB
/* 客户端内置公钥（raw 32 字节 hex）。换密钥只改这一行。 */
#define XLV_ED25519_PUB "23a3dc70629585297494197deeea4a0871b8c1d895e2d9518afc41c8cd1d2bf9"
#endif

namespace XLV {
namespace ed25519 {

struct Api {
    void *h_crypto;
    int  (*ED25519_verify)(const uint8_t *, size_t, const uint8_t *, const uint8_t *);
    void *(*EVP_PKEY_new_raw_public_key)(int, void *, const unsigned char *, size_t);
    void *(*EVP_MD_CTX_new)();
    void  (*EVP_MD_CTX_free)(void *);
    int  (*EVP_DigestVerifyInit)(void *, void **, const void *, void *, void *);
    int  (*EVP_DigestVerify)(void *, const unsigned char *, size_t, const unsigned char *, size_t);
    void (*EVP_PKEY_free)(void *);
};

static Api &api() {
    static Api a;
    static bool tried = false;
    if (tried) return a;
    tried = true;
    memset(&a, 0, sizeof(a));
    const char *cands[] = { "/system/lib64/libcrypto.so", "/system/lib/libcrypto.so",
                            "libcrypto.so.3", "libcrypto.so.1.1", "libcrypto.so" };
    for (size_t i = 0; i < sizeof(cands) / sizeof(cands[0]) && !a.h_crypto; ++i) {
        void *h = dlopen(cands[i], RTLD_NOW);
        if (!h) continue;
        a.ED25519_verify             = (int (*)(const uint8_t *, size_t, const uint8_t *, const uint8_t *))
                                       dlsym(h, "ED25519_verify");
        a.EVP_PKEY_new_raw_public_key = (void *(*)(int, void *, const unsigned char *, size_t))
                                       dlsym(h, "EVP_PKEY_new_raw_public_key");
        a.EVP_MD_CTX_new             = (void *(*)())dlsym(h, "EVP_MD_CTX_new");
        a.EVP_MD_CTX_free            = (void (*)(void *))dlsym(h, "EVP_MD_CTX_free");
        a.EVP_DigestVerifyInit       = (int (*)(void *, void **, const void *, void *, void *))
                                       dlsym(h, "EVP_DigestVerifyInit");
        a.EVP_DigestVerify           = (int (*)(void *, const unsigned char *, size_t,
                                                const unsigned char *, size_t))
                                       dlsym(h, "EVP_DigestVerify");
        a.EVP_PKEY_free              = (void (*)(void *))dlsym(h, "EVP_PKEY_free");
        if (a.ED25519_verify ||
            (a.EVP_PKEY_new_raw_public_key && a.EVP_MD_CTX_new && a.EVP_DigestVerifyInit &&
             a.EVP_DigestVerify && a.EVP_PKEY_free))
            a.h_crypto = h;
        else
            memset(&a, 0, sizeof(a));
    }
    return a;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* 把 64 位 hex 解析成 32 字节公钥 */
static bool pub_from_hex(const char *hex, unsigned char out[32]) {
    if (!hex) return false;
    if (strlen(hex) != 64) return false;
    for (int i = 0; i < 32; ++i) {
        int hi = hexval(hex[i * 2]), lo = hexval(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return true;
}

/* msg/签名(64B)/公钥(32B) */
static bool verify_raw(const unsigned char *msg, size_t mlen,
                       const unsigned char *sig, const unsigned char *pub) {
    Api &a = api();
    if (a.ED25519_verify) return a.ED25519_verify(msg, mlen, sig, pub) == 1;
    if (a.EVP_PKEY_new_raw_public_key) {
        /* NID_ED25519 = 1087 */
        void *pk = a.EVP_PKEY_new_raw_public_key(1087, NULL, pub, 32);
        if (!pk) return false;
        void *ctx = a.EVP_MD_CTX_new();
        bool ok = false;
        if (ctx) {
            if (a.EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, pk) == 1)
                ok = (a.EVP_DigestVerify(ctx, sig, 64, msg, mlen) == 1);
            a.EVP_MD_CTX_free(ctx);
        }
        a.EVP_PKEY_free(pk);
        return ok;
    }
    return false;
}

} /* namespace ed25519 */
} /* namespace XLV */
#endif /* XLV_ED25519_H */
