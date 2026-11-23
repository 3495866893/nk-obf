#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
XL云验证 —— AES-256-GCM 服务端参考实现（与客户端 res/Encrypt.h 完全对应）

协议约定
--------------------------------------------------------------
1. 密钥派生
   key = AESKEY 的原始 32 字节（当 len(AESKEY) == 32）
   key = SHA256(AESKEY)          （其它长度）
2. 线上格式（全部为小写十六进制字符串，可安全放进 URL-encoded body）
   payload = hex( IV(12B) || CIPHERTEXT(nB) || TAG(16B) )
3. AAD = 空
4. IV
   普通加密 : 12 字节随机（/dev/urandom）
   确定性加密（客户端 random_rc4 派生）: IV = SHA256(明文)[0:12]
5. 请求体
   data=<payload>&value=<random>     # api=kmlogon
   data=<payload>                    # api=kmunmachine
6. 响应体：把 JSON 文本用同一算法加密后整串返回；未加密的纯 JSON 也可直接返回
   （客户端先尝试 hex 解密，失败则按明文 JSON 解析）
--------------------------------------------------------------
依赖: pip install cryptography
"""
import hashlib
import os
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

AESKEY = b"mN4bV7cX1zL9kJ2hG5fD8sA0qW3enknb"   # 32 字节
APPKEY = b"y4sH2w11imYIx1K4"                  # 16 字节，仅用于 MD5 签名
APPID = "11054"


def derive_key(passwd: bytes) -> bytes:
    if len(passwd) == 32:
        return passwd
    return hashlib.sha256(passwd).digest()


def pack(plaintext, iv: bytes = None, key: bytes = None) -> str:
    """加密并打包为 hex(iv||ct||tag)。iv 为空则随机 12 字节。"""
    key = key or derive_key(AESKEY)
    if isinstance(plaintext, str):
        plaintext = plaintext.encode("utf-8")
    if iv is None:
        iv = os.urandom(12)
    ct_tag = AESGCM(key).encrypt(iv, plaintext, None)
    return (iv + ct_tag).hex()


def pack_deterministic(plaintext, key: bytes = None) -> str:
    """确定性加密：IV = SHA256(明文)[0:12]（对应客户端 EncryptDeterministic）"""
    key = key or derive_key(AESKEY)
    if isinstance(plaintext, str):
        plaintext = plaintext.encode("utf-8")
    iv = hashlib.sha256(plaintext).digest()[:12]
    return pack(plaintext, iv, key)


def unpack(payload_hex: str, key: bytes = None) -> bytes:
    """校验 tag 并解出明文；认证失败抛 InvalidTag"""
    key = key or derive_key(AESKEY)
    raw = bytes.fromhex(payload_hex)
    if len(raw) < 28:
        raise ValueError("payload too short")
    iv, rest = raw[:12], raw[12:]
    return AESGCM(key).decrypt(iv, rest, None)


def verify_random(markcode: str, t: str, random_md5_from_client: str) -> bool:
    """服务端复算 value 参数（与原 RC4 逻辑等价）"""
    random_src = ("%s独九月" % t).encode("utf-8")
    enc = pack_deterministic(random_src)
    want = hashlib.md5((enc + APPKEY.decode() + markcode).encode("utf-8")).hexdigest()
    return want == random_md5_from_client


if __name__ == "__main__":
    msg = "kami=ABCD-EFGH&markcode=0123456789abcdef&t=1700000000&sign=deadbeef"
    p = pack(msg)
    print("payload :", p)
    print("decrypt :", unpack(p).decode())
    d = pack_deterministic(msg)
    print("determ  :", d)
    print("det same:", d == pack_deterministic(msg))