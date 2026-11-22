#ifndef GM_H
#define GM_H
/* ==================================================================
   SM4-CBC(动态IV) + HMAC-SM3 传输封装（XL云验证 "SM4-CBC加密(动态IV+SM3签名)"）
   密文 = Base64( 16B IV + SM4-CBC( 8B随机 + 4B秒级时间戳[大端] + 业务参数 ) + 32B HMAC-SM3(key, IV+密文) )
   依赖：Encrypt.h 里的 Base64Encode / Base64Decode（必须先 include）
   ================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

static const unsigned char GM_SBOX[256] = {
    0xd6,0x90,0xe9,0xfe,0xcc,0xe1,0x3d,0xb7,0x16,0xb6,0x14,0xc2,0x28,0xfb,0x2c,0x05,
    0x2b,0x67,0x9a,0x76,0x2a,0xbe,0x04,0xc3,0xaa,0x44,0x13,0x26,0x49,0x86,0x06,0x99,
    0x9c,0x42,0x50,0xf4,0x91,0xef,0x98,0x7a,0x33,0x54,0x0b,0x43,0xed,0xcf,0xac,0x62,
    0xe4,0xb3,0x1c,0xa9,0xc9,0x08,0xe8,0x95,0x80,0xdf,0x94,0xfa,0x75,0x8f,0x3f,0xa6,
    0x47,0x07,0xa7,0xfc,0xf3,0x73,0x17,0xba,0x83,0x59,0x3c,0x19,0xe6,0x85,0x4f,0xa8,
    0x68,0x6b,0x81,0xb2,0x71,0x64,0xda,0x8b,0xf8,0xeb,0x0f,0x4b,0x70,0x56,0x9d,0x35,
    0x1e,0x24,0x0e,0x5e,0x63,0x58,0xd1,0xa2,0x25,0x22,0x7c,0x3b,0x01,0x21,0x78,0x87,
    0xd4,0x00,0x46,0x57,0x9f,0xd3,0x27,0x52,0x4c,0x36,0x02,0xe7,0xa0,0xc4,0xc8,0x9e,
    0xea,0xbf,0x8a,0xd2,0x40,0xc7,0x38,0xb5,0xa3,0xf7,0xf2,0xce,0xf9,0x61,0x15,0xa1,
    0xe0,0xae,0x5d,0xa4,0x9b,0x34,0x1a,0x55,0xad,0x93,0x32,0x30,0xf5,0x8c,0xb1,0xe3,
    0x1d,0xf6,0xe2,0x2e,0x82,0x66,0xca,0x60,0xc0,0x29,0x23,0xab,0x0d,0x53,0x4e,0x6f,
    0xd5,0xdb,0x37,0x45,0xde,0xfd,0x8e,0x2f,0x03,0xff,0x6a,0x72,0x6d,0x6c,0x5b,0x51,
    0x8d,0x1b,0xaf,0x92,0xbb,0xdd,0xbc,0x7f,0x11,0xd9,0x5c,0x41,0x1f,0x10,0x5a,0xd8,
    0x0a,0xc1,0x31,0x88,0xa5,0xcd,0x7b,0xbd,0x2d,0x74,0xd0,0x12,0xb8,0xe5,0xb4,0xb0,
    0x89,0x69,0x97,0x4a,0x0c,0x96,0x77,0x7e,0x65,0xb9,0xf1,0x09,0xc5,0x6e,0xc6,0x84,
    0x18,0xf0,0x7d,0xec,0x3a,0xdc,0x4d,0x20,0x79,0xee,0x5f,0x3e,0xd7,0xcb,0x39,0x48
};

static const uint32_t GM_CK[32] = {
    0x00070e15,0x1c232a31,0x383f464d,0x545b6269,0x70777e85,0x8c939aa1,0xa8afb6bd,0xc4cbd2d9,
    0xe0e7eef5,0xfc030a11,0x181f262d,0x343b4249,0x50575e65,0x6c737a81,0x888f969d,0xa4abb2b9,
    0xc0c7ced5,0xdce3eaf1,0xf8ff060d,0x141b2229,0x30373e45,0x4c535a61,0x686f767d,0x848b9299,
    0xa0a7aeb5,0xbcc3cad1,0xd8dfe6ed,0xf4fb0209,0x10171e25,0x2c333a41,0x484f565d,0x646b7279
};

static inline uint32_t gm_rotl(uint32_t x, int n) { n &= 31; return n ? ((x << n) | (x >> (32 - n))) : x; }

static inline uint32_t gm_tau(uint32_t a)
{
    return ((uint32_t)GM_SBOX[(a >> 24) & 0xff] << 24) | ((uint32_t)GM_SBOX[(a >> 16) & 0xff] << 16) |
           ((uint32_t)GM_SBOX[(a >> 8) & 0xff] << 8) | (uint32_t)GM_SBOX[a & 0xff];
}
static inline uint32_t gm_L(uint32_t b)  { return b ^ gm_rotl(b, 2) ^ gm_rotl(b, 10) ^ gm_rotl(b, 18) ^ gm_rotl(b, 24); }
static inline uint32_t gm_Lp(uint32_t b) { return b ^ gm_rotl(b, 13) ^ gm_rotl(b, 23); }

static inline uint32_t gm_be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static inline void gm_st32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

/* 密钥扩展：key 16 字节 -> 32 轮密钥 */
static void gm_sm4_setkey(const unsigned char key[16], uint32_t rk[32])
{
    static const uint32_t FK[4] = {0xa3b1bac6, 0x56aa3350, 0x677d9197, 0xb27022dc};
    uint32_t K[4];
    int i;
    for (i = 0; i < 4; i++) K[i] = gm_be32(key + i * 4) ^ FK[i];
    for (i = 0; i < 32; i++)
    {
        uint32_t b = K[1] ^ K[2] ^ K[3] ^ GM_CK[i];
        uint32_t nb = K[0] ^ gm_Lp(gm_tau(b));
        rk[i] = nb;
        K[0] = K[1]; K[1] = K[2]; K[2] = K[3]; K[3] = nb;
    }
}

static void gm_sm4_block(const uint32_t rk[32], const unsigned char in[16], unsigned char out[16])
{
    uint32_t X[4];
    int i;
    for (i = 0; i < 4; i++) X[i] = gm_be32(in + i * 4);
    for (i = 0; i < 32; i++)
    {
        uint32_t b = X[1] ^ X[2] ^ X[3] ^ rk[i];
        uint32_t nb = X[0] ^ gm_L(gm_tau(b));
        X[0] = X[1]; X[1] = X[2]; X[2] = X[3]; X[3] = nb;
    }
    for (i = 0; i < 4; i++) gm_st32(out + i * 4, X[3 - i]);
}

static void gm_sm4_cbc_encrypt(const unsigned char key[16], const unsigned char iv[16],
                               const unsigned char *pt, int len, unsigned char *ct)
{
    uint32_t rk[32];
    unsigned char prev[16], blk[16];
    int i, off;
    gm_sm4_setkey(key, rk);
    memcpy(prev, iv, 16);
    for (off = 0; off < len; off += 16)
    {
        for (i = 0; i < 16; i++) blk[i] = pt[off + i] ^ prev[i];
        gm_sm4_block(rk, blk, ct + off);
        memcpy(prev, ct + off, 16);
    }
}

static void gm_sm4_cbc_decrypt(const unsigned char key[16], const unsigned char iv[16],
                               const unsigned char *ct, int len, unsigned char *pt)
{
    uint32_t rk[32], drk[32];
    unsigned char prev[16], blk[16];
    int i, off;
    gm_sm4_setkey(key, rk);
    for (i = 0; i < 32; i++) drk[i] = rk[31 - i];   /* SM4 解密 = 逆序使用轮密钥 */
    memcpy(prev, iv, 16);
    for (off = 0; off < len; off += 16)
    {
        gm_sm4_block(drk, ct + off, blk);
        for (i = 0; i < 16; i++) pt[off + i] = blk[i] ^ prev[i];
        memcpy(prev, ct + off, 16);
    }
}

/* ---------------- SM3 ---------------- */
static inline uint32_t gm_p0(uint32_t x) { return x ^ gm_rotl(x, 9) ^ gm_rotl(x, 17); }
static inline uint32_t gm_p1(uint32_t x) { return x ^ gm_rotl(x, 15) ^ gm_rotl(x, 23); }

static void gm_sm3_block(uint32_t V[8], const unsigned char blk[64])
{
    uint32_t W[68], W1[64];
    uint32_t A, B, C, D, E, F, G, H, SS1, SS2, TT1, TT2, Tj;
    int j;
    for (j = 0; j < 16; j++) W[j] = gm_be32(blk + j * 4);
    for (j = 16; j < 68; j++)
        W[j] = gm_p1(W[j - 16] ^ W[j - 9] ^ gm_rotl(W[j - 3], 15)) ^ gm_rotl(W[j - 13], 7) ^ W[j - 6];
    for (j = 0; j < 64; j++) W1[j] = W[j] ^ W[j + 4];

    A = V[0]; B = V[1]; C = V[2]; D = V[3];
    E = V[4]; F = V[5]; G = V[6]; H = V[7];
    for (j = 0; j < 64; j++)
    {
        Tj = (j < 16) ? 0x79cc4519u : 0x7a879d8au;
        SS1 = gm_rotl((gm_rotl(A, 12) + E + gm_rotl(Tj, j & 31)) & 0xffffffffu, 7);
        SS2 = SS1 ^ gm_rotl(A, 12);
        TT1 = ((j < 16) ? (A ^ B ^ C) : ((A & B) | (A & C) | (B & C))) + D + SS2 + W1[j];
        TT2 = ((j < 16) ? (E ^ F ^ G) : ((E & F) | ((~E) & G))) + H + SS1 + W[j];
        D = C; C = gm_rotl(B, 9); B = A; A = TT1;
        H = G; G = gm_rotl(F, 19); F = E; E = gm_p0(TT2);
    }
    V[0] ^= A; V[1] ^= B; V[2] ^= C; V[3] ^= D;
    V[4] ^= E; V[5] ^= F; V[6] ^= G; V[7] ^= H;
}

static void gm_sm3_hash(const unsigned char *data, int len, unsigned char out[32])
{
    uint32_t V[8] = {0x7380166f, 0x4914b2b9, 0x172442d7, 0xda8a0600,
                     0xa96f30bc, 0x163138aa, 0xe38dee4d, 0xb0fb0e4e};
    unsigned char blk[64];
    int i, off = 0, rest;
    unsigned long long bits;

    while (len - off >= 64)
    {
        gm_sm3_block(V, data + off);
        off += 64;
    }
    rest = len - off;
    memset(blk, 0, 64);
    if (rest) memcpy(blk, data + off, rest);
    blk[rest] = 0x80;
    if (rest >= 56)
    {
        gm_sm3_block(V, blk);
        memset(blk, 0, 64);
    }
    bits = (unsigned long long)len * 8ULL;
    for (i = 0; i < 8; i++) blk[63 - i] = (unsigned char)(bits >> (8 * i));
    gm_sm3_block(V, blk);
    for (i = 0; i < 8; i++) gm_st32(out + i * 4, V[i]);
}

static void gm_hmac_sm3(const unsigned char *key, int klen,
                        const unsigned char *msg, int mlen, unsigned char out[32])
{
    unsigned char k[64], inner[32];
    unsigned char *buf;
    int i;
    memset(k, 0, 64);
    if (klen > 64) gm_sm3_hash(key, klen, k);
    else memcpy(k, key, klen);

    /* 缓冲区要同时容纳 ipad‖msg 和 opad‖inner(32字节)，取两者较大者 */
    {
        int need = 64 + (mlen > 32 ? mlen : 32);
        buf = (unsigned char *)malloc(need);
        if (!buf) return;
    }
    for (i = 0; i < 64; i++) buf[i] = k[i] ^ 0x36;
    if (mlen > 0) memcpy(buf + 64, msg, mlen);
    gm_sm3_hash(buf, 64 + mlen, inner);

    for (i = 0; i < 64; i++) buf[i] = k[i] ^ 0x5c;
    memcpy(buf + 64, inner, 32);
    gm_sm3_hash(buf, 96, out);
    free(buf);
}

static void gm_rand_bytes(unsigned char *out, int n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (f)
    {
        if (fread(out, 1, n, f) != (size_t)n) { int i; for (i = 0; i < n; i++) out[i] = (unsigned char)(rand() & 0xff); }
        fclose(f);
    }
    else { int i; for (i = 0; i < n; i++) out[i] = (unsigned char)(rand() & 0xff); }
}

/* 加密：明文 -> Base64(IV + 密文 + HMAC-SM3) */
static char *GmWrap(const char *plain, const char *key)
{
#ifdef GM_DEBUG
#endif
    int plen = (int)strlen(plain);
    int total = 12 + plen;
    int padlen = 16 - (total % 16);
    if (padlen == 0) padlen = 16;
    int ctlen = total + padlen;
    int alllen = 16 + ctlen + 32;

    unsigned char *pt = new unsigned char[ctlen];
    unsigned char *ct = new unsigned char[ctlen];
    unsigned char *all = new unsigned char[alllen];
    unsigned char iv[16], tag[32], k[16];
    uint32_t t;
    char *b64;

    gm_rand_bytes(pt, 8);
    t = (uint32_t)time(NULL);
    pt[8] = (unsigned char)(t >> 24); pt[9] = (unsigned char)(t >> 16);
    pt[10] = (unsigned char)(t >> 8); pt[11] = (unsigned char)t;
    memcpy(pt + 12, plain, plen);
    memset(pt + total, padlen, padlen);

    gm_rand_bytes(iv, 16);
    memcpy(k, key, 16);
    gm_sm4_cbc_encrypt(k, iv, pt, ctlen, ct);
#ifdef GM_DEBUG
#endif

    memcpy(all, iv, 16);
    memcpy(all + 16, ct, ctlen);
    gm_hmac_sm3(k, 16, all, 16 + ctlen, tag);      /* tag = HMAC-SM3(key, IV||密文) */
    memcpy(all + 16 + ctlen, tag, 32);
#ifdef GM_DEBUG
#endif

    b64 = Base64Encode(all, alllen);
#ifdef GM_DEBUG
#endif
    delete[] pt; delete[] ct; delete[] all;
    return b64;
}

/* 解密：Base64(IV + 密文 + HMAC-SM3) -> 去掉 12 字节包装头后的明文（JSON） */
static char *GmUnwrap(const char *b64, const char *key)
{
    int rawlen = 0;
    if (!b64) return NULL;
    unsigned char *raw = Base64Decode(b64, &rawlen);
    if (!raw || rawlen < 48) { if (raw) delete[] raw; return NULL; }
    int ctlen = rawlen - 48;
    if (ctlen <= 0 || (ctlen % 16)) { delete[] raw; return NULL; }

    unsigned char k[16], calc[32];
    unsigned char *pt = NULL;
    memcpy(k, key, 16);
    /* ---- 加固第 5 条：HMAC-SM3 标签必须校验，不通过直接拒绝 ----
       此前这里算了标签却不比对，攻击者可以任意改密文/IV 而不被发现，
       甚至能构造"服务端从不签名"的假响应。现在强制 memcmp，不等即 fail。 */
    gm_hmac_sm3(k, 16, raw, 16 + ctlen, calc);
    if (memcmp(calc, raw + 16 + ctlen, 32) != 0) {
        delete[] raw;
        return NULL;
    }
    pt = new unsigned char[ctlen];
    gm_sm4_cbc_decrypt(k, raw, raw + 16, ctlen, pt);

    int total = ctlen;
    if (total > 0 && pt[total - 1] >= 1 && pt[total - 1] <= 16) total -= pt[total - 1];
    if (total < 12) { delete[] pt; delete[] raw; return NULL; }

    char *out = new char[total - 12 + 1];
    memcpy(out, pt + 12, total - 12);
    out[total - 12] = 0;
    delete[] pt;
    delete[] raw;
    return out;
}

#endif /* GM_H */