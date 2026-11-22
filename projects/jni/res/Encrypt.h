#ifndef _AES256_GCM_ENCRYPT_H_
#define _AES256_GCM_ENCRYPT_H_
/*
 * Encrypt.h  —  AES-256-GCM 标准实现（替代原 RC4）
 * ---------------------------------------------------------------------------
 * 算法 : AES-256-GCM (NIST SP 800-38D / FIPS-197)，纯 C++ 实现，无第三方依赖
 * 认证 : GHASH(H) 128-bit tag，AAD 固定为空
 * 封装 : 发送端输出 hex( IV(12) || CIPHERTEXT(n) || TAG(16) )
 * 接收端输入 同上格式的 hex 字符串
 * 密钥 : 口令长度 == 32 时直接作为 256-bit 原始密钥；否则 key = SHA256(口令)
 * 随机 IV : /dev/urandom；确定性模式 IV = SHA256(明文)[0..11]
 * ---------------------------------------------------------------------------
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#define AES_BLOCK_LEN   16
#define AES256_KEY_LEN  32
#define AES256_RK_LEN   240
#define GCM_IV_LEN      12
#define GCM_TAG_LEN     16

/* 对外接口（与原 RC4 版本保持完全一致，pz.h 无需改动函数名） */
char* Encrypt(const char* szSource, const char* szPassWord);             /* AES-256-GCM，随机 IV，输出 Base64 */
char* EncryptDeterministic(const char* szSource, const char* szPassWord);/* AES-256-GCM，IV=SHA256(明文)[0..11]，输出 Base64 */
char* Decrypt(const char* szSource, const char* szPassWord);             /* 自动识别 Base64 / Hex，失败返回 NULL */

char* ByteToHex(const unsigned char* vByte, const int vLen);
unsigned char* HexToByte(const char* szHex);
char* Base64Encode(const unsigned char* data, int len);
unsigned char* Base64Decode(const char* s, int* out_len);
char* UrlEncode(const char* s);

/* ========================= 基础表 ========================= */

static const unsigned char AES_SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const unsigned char AES_RCON[15] = {
    0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36,0x6c,0xd8,0xab,0x4d
};

/* ========================= SHA-256（用于密钥派生 / 确定性 IV） ========================= */

typedef struct { uint32_t h[8]; uint64_t len; unsigned char buf[64]; size_t idx; } sha256_ctx;

static uint32_t sha256_rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha256_init(sha256_ctx* c)
{
    c->h[0]=0x6a09e667; c->h[1]=0xbb67ae85; c->h[2]=0x3c6ef372; c->h[3]=0xa54ff53a;
    c->h[4]=0x510e527f; c->h[5]=0x9b05688c; c->h[6]=0x1f83d9ab; c->h[7]=0x5be0cd19;
    c->len=0; c->idx=0;
}

static void sha256_block(sha256_ctx* c, const unsigned char* p)
{
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) | ((uint32_t)p[i*4+2] << 8) | p[i*4+3];
    for (int i = 16; i < 64; ++i)
    {
        uint32_t s0 = sha256_rotr(w[i-15],7) ^ sha256_rotr(w[i-15],18) ^ (w[i-15] >> 3);
        uint32_t s1 = sha256_rotr(w[i-2],17) ^ sha256_rotr(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=c->h[0],b=c->h[1],d=c->h[2],e=c->h[3],f=c->h[4],g=c->h[5],h=c->h[6],k=c->h[7];
    for (int i = 0; i < 64; ++i)
    {
        uint32_t S1 = sha256_rotr(f,6) ^ sha256_rotr(f,11) ^ sha256_rotr(f,25);
        uint32_t ch = (f & g) ^ ((~f) & h);
        uint32_t t1 = k + S1 + ch + K[i] + w[i];
        uint32_t S0 = sha256_rotr(a,2) ^ sha256_rotr(a,13) ^ sha256_rotr(a,22);
        uint32_t mj = (a & b) ^ (a & d) ^ (b & d);
        uint32_t t2 = S0 + mj;
        k = h; h = g; g = f; f = e + t1; e = d; d = b; b = a; a = t1 + t2;
    }
    c->h[0]+=a; c->h[1]+=b; c->h[2]+=d; c->h[3]+=e; c->h[4]+=f; c->h[5]+=g; c->h[6]+=h; c->h[7]+=k;
}

static void sha256_update(sha256_ctx* c, const unsigned char* data, size_t len)
{
    c->len += (uint64_t)len;
    while (len)
    {
        size_t n = 64 - c->idx;
        if (n > len) n = len;
        memcpy(c->buf + c->idx, data, n);
        c->idx += n; data += n; len -= n;
        if (c->idx == 64) { sha256_block(c, c->buf); c->idx = 0; }
    }
}

static void sha256_final(sha256_ctx* c, unsigned char out[32])
{
    uint64_t bits = c->len * 8;
    unsigned char pad = 0x80;
    sha256_update(c, &pad, 1);
    unsigned char z = 0;
    while (c->idx != 56) sha256_update(c, &z, 1);
    unsigned char lb[8];
    for (int i = 0; i < 8; ++i) lb[i] = (unsigned char)(bits >> (56 - 8 * i));
    sha256_update(c, lb, 8);
    for (int i = 0; i < 8; ++i)
    {
        out[i*4]   = (unsigned char)(c->h[i] >> 24);
        out[i*4+1] = (unsigned char)(c->h[i] >> 16);
        out[i*4+2] = (unsigned char)(c->h[i] >> 8);
        out[i*4+3] = (unsigned char)(c->h[i]);
    }
    memset(c, 0, sizeof(*c));
}

static void sha256_buf(const unsigned char* data, size_t len, unsigned char out[32])
{
    sha256_ctx c; sha256_init(&c); sha256_update(&c, data, len); sha256_final(&c, out);
}

/* ========================= AES-256 分组加密 ========================= */

static unsigned char aes_xtime(unsigned char x)
{
    return (unsigned char)((x << 1) ^ ((x >> 7) ? 0x1b : 0x00));
}

static void aes256_key_expand(const unsigned char key[AES256_KEY_LEN], unsigned char rk[AES256_RK_LEN])
{
    int i;
    memcpy(rk, key, AES256_KEY_LEN);
    int rcon_i = 1;
    for (i = AES256_KEY_LEN; i < AES256_RK_LEN; i += 4)
    {
        unsigned char t0 = rk[i-4], t1 = rk[i-3], t2 = rk[i-2], t3 = rk[i-1];
        if (i % AES256_KEY_LEN == 0)
        {
            /* RotWord + SubWord + Rcon */
            unsigned char n0 = AES_SBOX[t1], n1 = AES_SBOX[t2], n2 = AES_SBOX[t3], n3 = AES_SBOX[t0];
            t0 = (unsigned char)(n0 ^ AES_RCON[rcon_i++]);
            t1 = n1; t2 = n2; t3 = n3;
        }
        else if (i % AES256_KEY_LEN == 16)
        {
            t0 = AES_SBOX[t0]; t1 = AES_SBOX[t1]; t2 = AES_SBOX[t2]; t3 = AES_SBOX[t3];
        }
        rk[i]   = (unsigned char)(rk[i - AES256_KEY_LEN]     ^ t0);
        rk[i+1] = (unsigned char)(rk[i - AES256_KEY_LEN + 1] ^ t1);
        rk[i+2] = (unsigned char)(rk[i - AES256_KEY_LEN + 2] ^ t2);
        rk[i+3] = (unsigned char)(rk[i - AES256_KEY_LEN + 3] ^ t3);
    }
}

static void aes256_encrypt_block(const unsigned char rk[AES256_RK_LEN],
                                 const unsigned char in[AES_BLOCK_LEN],
                                 unsigned char out[AES_BLOCK_LEN])
{
    unsigned char s[16];
    unsigned char t;
    int i, c;
    memcpy(s, in, 16);
    for (i = 0; i < 16; ++i) s[i] ^= rk[i];

    for (int round = 1; round <= 14; ++round)
    {
        /* SubBytes */
        for (i = 0; i < 16; ++i) s[i] = AES_SBOX[s[i]];
        /* ShiftRows（state[r][c] = s[r + 4c]） */
        t = s[1];  s[1]  = s[5];  s[5]  = s[9];  s[9]  = s[13]; s[13] = t;
        t = s[2];  s[2]  = s[10]; s[10] = t;
        t = s[6];  s[6]  = s[14]; s[14] = t;
        t = s[15]; s[15] = s[11]; s[11] = s[7];  s[7]  = s[3];  s[3]  = t;
        /* MixColumns */
        if (round != 14)
        {
            for (c = 0; c < 4; ++c)
            {
                unsigned char* p = s + c * 4;
                unsigned char a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
                p[0] = (unsigned char)(aes_xtime(a0) ^ (aes_xtime(a1) ^ a1) ^ a2 ^ a3);
                p[1] = (unsigned char)(a0 ^ aes_xtime(a1) ^ (aes_xtime(a2) ^ a2) ^ a3);
                p[2] = (unsigned char)(a0 ^ a1 ^ aes_xtime(a2) ^ (aes_xtime(a3) ^ a3));
                p[3] = (unsigned char)((aes_xtime(a0) ^ a0) ^ a1 ^ a2 ^ aes_xtime(a3));
            }
        }
        for (i = 0; i < 16; ++i) s[i] ^= rk[round * 16 + i];
    }
    memcpy(out, s, 16);
}

/* ========================= GHASH / GCM ========================= */

static void gcm_gf_mult(const unsigned char x[16], const unsigned char y[16], unsigned char z[16])
{
    unsigned char v[16];
    int i, j;
    memcpy(v, y, 16);
    memset(z, 0, 16);
    for (i = 0; i < 128; ++i)
    {
        if (x[i >> 3] & (0x80 >> (i & 7)))
            for (j = 0; j < 16; ++j) z[j] ^= v[j];
        int lsb = v[15] & 1;
        for (j = 15; j > 0; --j) v[j] = (unsigned char)((v[j] >> 1) | ((v[j-1] & 1) << 7));
        v[0] = (unsigned char)(v[0] >> 1);
        if (lsb) v[0] ^= 0xe1;
    }
}

typedef struct { unsigned char y[16]; unsigned char H[16]; } ghash_ctx;

static void ghash_init(ghash_ctx* g, const unsigned char H[16])
{
    memset(g->y, 0, 16);
    memcpy(g->H, H, 16);
}

/* 按 16 字节分块吸收，末尾不足 16 字节补零（符合 GCM 对每个输入串的处理规则） */
static void ghash_update(ghash_ctx* g, const unsigned char* data, size_t len)
{
    unsigned char blk[16];
    unsigned char tmp[16];
    size_t i = 0;
    while (i < len)
    {
        size_t n = len - i;
        if (n > 16) n = 16;
        if (n == 16) memcpy(blk, data + i, 16);
        else { memset(blk, 0, 16); memcpy(blk, data + i, n); }
        for (int j = 0; j < 16; ++j) g->y[j] ^= blk[j];
        gcm_gf_mult(g->y, g->H, tmp);
        memcpy(g->y, tmp, 16);
        i += n;
    }
}

static void ghash_len_block(ghash_ctx* g, uint64_t aad_bits, uint64_t ct_bits)
{
    unsigned char blk[16], tmp[16];
    for (int i = 0; i < 8; ++i) blk[i]     = (unsigned char)(aad_bits >> (56 - 8 * i));
    for (int i = 0; i < 8; ++i) blk[8 + i] = (unsigned char)(ct_bits  >> (56 - 8 * i));
    for (int j = 0; j < 16; ++j) g->y[j] ^= blk[j];
    gcm_gf_mult(g->y, g->H, tmp);
    memcpy(g->y, tmp, 16);
}

static void gcm_inc32(unsigned char ctr[16])
{
    for (int i = 15; i >= 12; --i)
    {
        if (++ctr[i] != 0) break;
    }
}

static void gcm_compute_j0(const unsigned char H[16], const unsigned char* iv, size_t ivlen, unsigned char J0[16])
{
    if (ivlen == GCM_IV_LEN)
    {
        memcpy(J0, iv, GCM_IV_LEN);
        J0[12] = 0; J0[13] = 0; J0[14] = 0; J0[15] = 1;
        return;
    }
    ghash_ctx g;
    ghash_init(&g, H);
    ghash_update(&g, iv, ivlen);
    ghash_len_block(&g, 0, (uint64_t)ivlen * 8);
    memcpy(J0, g.y, 16);
}

/*
 * AES-256-GCM 加密
 * aad 可为 NULL / aadlen=0
 * ct 与 pt 可为同一缓冲区
 * 返回 0 成功
 */
static int aes256_gcm_encrypt(const unsigned char* key, const unsigned char* iv, size_t ivlen,
                             const unsigned char* aad, size_t aadlen,
                             const unsigned char* pt, size_t ptlen,
                             unsigned char* ct, unsigned char tag[GCM_TAG_LEN])
{
    unsigned char rk[AES256_RK_LEN];
    unsigned char H[16], zero[16], J0[16], ctr[16], ks[16], s[16];
    ghash_ctx g;
    size_t i;

    aes256_key_expand(key, rk);
    memset(zero, 0, 16);
    aes256_encrypt_block(rk, zero, H);
    gcm_compute_j0(H, iv, ivlen, J0);

    /* CTR 加密 */
    memcpy(ctr, J0, 16);
    for (i = 0; i < ptlen; i += 16)
    {
        size_t n = ptlen - i;
        if (n > 16) n = 16;
        gcm_inc32(ctr);
        aes256_encrypt_block(rk, ctr, ks);
        for (size_t j = 0; j < n; ++j) ct[i + j] = (unsigned char)(pt[i + j] ^ ks[j]);
    }

    /* tag = E(K,J0) XOR GHASH(AAD || C || len) */
    ghash_init(&g, H);
    if (aad && aadlen) ghash_update(&g, aad, aadlen);
    ghash_update(&g, ct, ptlen);
    ghash_len_block(&g, (uint64_t)aadlen * 8, (uint64_t)ptlen * 8);
    aes256_encrypt_block(rk, J0, s);
    for (i = 0; i < 16; ++i) tag[i] = (unsigned char)(s[i] ^ g.y[i]);

    memset(rk, 0, sizeof(rk));
    memset(ks, 0, sizeof(ks));
    memset(s, 0, sizeof(s));
    return 0;
}

/*
 * AES-256-GCM 解密（先校验 tag，再解出明文）
 * 返回 0 成功；-1 tag 校验失败/参数错误
 */
static int aes256_gcm_decrypt(const unsigned char* key, const unsigned char* iv, size_t ivlen,
                             const unsigned char* aad, size_t aadlen,
                             const unsigned char* ct, size_t ctlen,
                             const unsigned char tag_in[GCM_TAG_LEN],
                             unsigned char* pt)
{
    unsigned char rk[AES256_RK_LEN];
    unsigned char H[16], zero[16], J0[16], ctr[16], ks[16], s[16], tag[GCM_TAG_LEN];
    ghash_ctx g;
    size_t i;
    unsigned char diff = 0;

    if (!key || !iv || !ct || !tag_in || !pt) return -1;
    if (ctlen && !ct) return -1;

    aes256_key_expand(key, rk);
    memset(zero, 0, 16);
    aes256_encrypt_block(rk, zero, H);
    gcm_compute_j0(H, iv, ivlen, J0);

    ghash_init(&g, H);
    if (aad && aadlen) ghash_update(&g, aad, aadlen);
    ghash_update(&g, ct, ctlen);
    ghash_len_block(&g, (uint64_t)aadlen * 8, (uint64_t)ctlen * 8);
    aes256_encrypt_block(rk, J0, s);
    for (i = 0; i < 16; ++i) tag[i] = (unsigned char)(s[i] ^ g.y[i]);

    for (i = 0; i < 16; ++i) diff |= (unsigned char)(tag[i] ^ tag_in[i]);
    memset(rk, 0, sizeof(rk));
    memset(ks, 0, sizeof(ks));
    memset(s, 0, sizeof(s));
    if (diff != 0) return -1;

    /* tag 已通过，再做 CTR 解密 */
    unsigned char rk2[AES256_RK_LEN];
    aes256_key_expand(key, rk2);
    memcpy(ctr, J0, 16);
    for (i = 0; i < ctlen; i += 16)
    {
        size_t n = ctlen - i;
        if (n > 16) n = 16;
        gcm_inc32(ctr);
        aes256_encrypt_block(rk2, ctr, ks);
        for (size_t j = 0; j < n; ++j) pt[i + j] = (unsigned char)(ct[i + j] ^ ks[j]);
    }
    memset(rk2, 0, sizeof(rk2));
    memset(ks, 0, sizeof(ks));
    return 0;
}

/* ========================= 工具函数 ========================= */

/* 字节码 -> 十六进制字符串（小写），new char[] 分配，调用方 delete[] */
char* ByteToHex(const unsigned char* vByte, const int vLen)
{
    if (!vByte || vLen <= 0) return NULL;
    char* tmp = new char[vLen * 2 + 1];
    static const char* hexd = "0123456789abcdef";
    for (int i = 0; i < vLen; ++i)
    {
        tmp[i * 2]     = hexd[(vByte[i] >> 4) & 0x0f];
        tmp[i * 2 + 1] = hexd[vByte[i] & 0x0f];
    }
    tmp[vLen * 2] = '\0';
    return tmp;
}

/* 十六进制字符串 -> 字节码，new unsigned char[] 分配，调用方 delete[] */
unsigned char* HexToByte(const char* szHex)
{
    if (!szHex) return NULL;
    int iLen = (int)strlen(szHex);
    if (iLen <= 0 || (iLen % 2) != 0) return NULL;
    unsigned char* pbBuf = new unsigned char[iLen / 2];
    for (int i = 0; i < iLen / 2; ++i)
    {
        char h = szHex[i * 2], l = szHex[i * 2 + 1];
        int t1 = (h >= '0' && h <= '9') ? h - '0' : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
        int t2 = (l >= '0' && l <= '9') ? l - '0' : (l >= 'a' && l <= 'f') ? l - 'a' + 10 : (l >= 'A' && l <= 'F') ? l - 'A' + 10 : -1;
        if (t1 < 0 || t2 < 0) { delete[] pbBuf; return NULL; }
        pbBuf[i] = (unsigned char)((t1 << 4) | t2);
    }
    return pbBuf;
}

/* 字节码 -> Base64 字符串（带 '=' 填充，标准字母表），调用方 delete[] */
char* Base64Encode(const unsigned char* data, int len)
{
    static const char* tb = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (!data || len <= 0) { char* e = new char[1]; e[0] = 0; return e; }
    int outlen = ((len + 2) / 3) * 4;
    char* out = new char[outlen + 1];
    int i, o = 0;
    for (i = 0; i + 2 < len; i += 3)
    {
        unsigned int v = ((unsigned int)data[i] << 16) | ((unsigned int)data[i + 1] << 8) | data[i + 2];
        out[o++] = tb[(v >> 18) & 0x3f];
        out[o++] = tb[(v >> 12) & 0x3f];
        out[o++] = tb[(v >> 6) & 0x3f];
        out[o++] = tb[v & 0x3f];
    }
    if (i < len)
    {
        unsigned int v = (unsigned int)data[i] << 16;
        if (i + 1 < len) v |= (unsigned int)data[i + 1] << 8;
        out[o++] = tb[(v >> 18) & 0x3f];
        out[o++] = tb[(v >> 12) & 0x3f];
        out[o++] = (i + 1 < len) ? tb[(v >> 6) & 0x3f] : '=';
        out[o++] = '=';
    }
    out[o] = 0;
    return out;
}

/* Base64 字符串 -> 字节码，忽略空白与非法字符；失败返回 NULL。调用方 delete[] */
unsigned char* Base64Decode(const char* s, int* out_len)
{
    if (!s) return NULL;
    static int rev[256];
    static bool rev_init = false;
    if (!rev_init)
    {
        for (int i = 0; i < 256; ++i) rev[i] = -1;
        const char* tb = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) rev[(unsigned char)tb[i]] = i;
        rev_init = true;
    }
    size_t n = strlen(s);
    unsigned char* out = new unsigned char[n * 3 / 4 + 4];
    int o = 0;
    unsigned int buf = 0;
    int bits = 0;
    for (size_t i = 0; i < n; ++i)
    {
        unsigned char c = (unsigned char)s[i];
        if (c == '=') break;
        int v = rev[c];
        if (v < 0) continue;               /* 跳过换行/空格/回车等 */
        buf = (buf << 6) | (unsigned int)v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out[o++] = (unsigned char)((buf >> bits) & 0xff);
        }
    }
    if (o == 0) { delete[] out; return NULL; }
    if (out_len) *out_len = o;
    return out;
}

/* URL 编码（application/x-www-form-urlencoded）：Base64 的 + / = 必须转义 */
char* UrlEncode(const char* s)
{
    if (!s) return NULL;
    static const char* hexd = "0123456789ABCDEF";
    size_t n = strlen(s);
    char* out = new char[n * 3 + 1];
    size_t o = 0;
    for (size_t i = 0; i < n; ++i)
    {
        unsigned char c = (unsigned char)s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
        {
            out[o++] = (char)c;
        }
        else
        {
            out[o++] = '%';
            out[o++] = hexd[(c >> 4) & 0x0f];
            out[o++] = hexd[c & 0x0f];
        }
    }
    out[o] = 0;
    return out;
}

/* 口令 -> 256bit 密钥：32 字节原样使用，否则 SHA256(口令) */
static void aes_gcm_derive_key(const char* pass, unsigned char key[32])
{
    size_t n = pass ? strlen(pass) : 0;
    if (n == AES256_KEY_LEN)
    {
        memcpy(key, pass, 32);
        return;
    }
    if (n == 0) { memset(key, 0, 32); return; }
    sha256_buf((const unsigned char*)pass, n, key);
}

/* 随机 IV：优先 /dev/urandom，失败则用 时间^pid 混合 */
static void aes_gcm_random_iv(unsigned char iv[GCM_IV_LEN])
{
    FILE* fp = fopen("/dev/urandom", "rb");
    if (fp)
    {
        size_t got = fread(iv, 1, GCM_IV_LEN, fp);
        fclose(fp);
        if (got == GCM_IV_LEN) return;
    }
    unsigned char seed[16];
    unsigned int t = (unsigned int)time(NULL);
    unsigned int p = (unsigned int)getpid();
    memcpy(seed, &t, 4);
    memcpy(seed + 4, &p, 4);
    memset(seed + 8, 0, 8);
    unsigned char h[32];
    sha256_buf(seed, 16, h);
    memcpy(iv, h, GCM_IV_LEN);
}

/* ========================= 对外封装 ========================= */

/*
 * 加密：随机 12 字节 IV
 * 输出：hex( IV || CIPHERTEXT || TAG )
 */
char* Encrypt(const char* szSource, const char* szPassWord)
{
    if (!szSource || !szPassWord) return NULL;

    unsigned char key[32];
    unsigned char iv[GCM_IV_LEN];
    size_t ptlen = strlen(szSource);
    size_t total = GCM_IV_LEN + ptlen + GCM_TAG_LEN;

    aes_gcm_derive_key(szPassWord, key);
    aes_gcm_random_iv(iv);

    unsigned char* buf = new unsigned char[total];
    memset(buf, 0, total);
    memcpy(buf, iv, GCM_IV_LEN);

    int r = aes256_gcm_encrypt(key, iv, GCM_IV_LEN, NULL, 0,
                               (const unsigned char*)szSource, ptlen,
                               buf + GCM_IV_LEN, buf + GCM_IV_LEN + ptlen);
    memset(key, 0, sizeof(key));
    if (r != 0) { delete[] buf; return NULL; }

    char* b64out = Base64Encode(buf, (int)total);
    delete[] buf;
    return b64out;
}

/*
 * 确定性加密：IV = SHA256(明文)[0..11]
 * 用于需要服务端可复现的场景（原 RC4 的 random_rc4 派生）
 */
char* EncryptDeterministic(const char* szSource, const char* szPassWord)
{
    if (!szSource || !szPassWord) return NULL;

    unsigned char key[32];
    unsigned char iv[GCM_IV_LEN];
    size_t ptlen = strlen(szSource);
    size_t total = GCM_IV_LEN + ptlen + GCM_TAG_LEN;

    aes_gcm_derive_key(szPassWord, key);
    unsigned char dig[32];
    sha256_buf((const unsigned char*)szSource, ptlen, dig);
    memcpy(iv, dig, GCM_IV_LEN);
    memset(dig, 0, sizeof(dig));

    unsigned char* buf = new unsigned char[total];
    memset(buf, 0, total);
    memcpy(buf, iv, GCM_IV_LEN);

    int r = aes256_gcm_encrypt(key, iv, GCM_IV_LEN, NULL, 0,
                               (const unsigned char*)szSource, ptlen,
                               buf + GCM_IV_LEN, buf + GCM_IV_LEN + ptlen);
    memset(key, 0, sizeof(key));
    if (r != 0) { delete[] buf; return NULL; }

    char* b64out = Base64Encode(buf, (int)total);
    delete[] buf;
    return b64out;
}

/*
 * 解密：服务端标准格式 Base64( IV || CIPHERTEXT || TAG )；同时兼容旧的 Hex 格式
 * 认证失败 / 格式错误返回 NULL
 */
char* Decrypt(const char* szSource, const char* szPassWord)
{
    if (!szSource || !szPassWord) return NULL;

    const char* s = szSource;
    while (*s == ' ' || *s == '\r' || *s == '\n' || *s == '\t') ++s;
    size_t slen = strlen(s);
    if (slen < (size_t)(GCM_IV_LEN + GCM_TAG_LEN)) return NULL;

    unsigned char* raw = NULL;
    int total = 0;

    /* 纯 hex 且长度合法 -> 按 hex 解；否则按 Base64 解 */
    bool all_hex = (slen % 2 == 0);
    for (size_t i = 0; all_hex && i < slen; ++i)
    {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            all_hex = false;
    }
    if (all_hex)
    {
        raw = HexToByte(s);
        if (raw) total = (int)(slen / 2);
    }
    if (!raw)
        raw = Base64Decode(s, &total);

    if (!raw) return NULL;
    if (total < GCM_IV_LEN + GCM_TAG_LEN) { delete[] raw; return NULL; }

    size_t ctlen = (size_t)total - GCM_IV_LEN - GCM_TAG_LEN;
    const unsigned char* iv  = raw;
    const unsigned char* ct  = raw + GCM_IV_LEN;
    const unsigned char* tag = raw + GCM_IV_LEN + ctlen;

    unsigned char key[32];
    aes_gcm_derive_key(szPassWord, key);

    char* out = new char[ctlen + 1];
    memset(out, 0, ctlen + 1);

    int r = aes256_gcm_decrypt(key, iv, GCM_IV_LEN, NULL, 0, ct, ctlen, tag, (unsigned char*)out);
    memset(key, 0, sizeof(key));
    delete[] raw;
    if (r != 0) { delete[] out; return NULL; }
    out[ctlen] = '\0';
    return out;
}

#endif /* _AES256_GCM_ENCRYPT_H_ */