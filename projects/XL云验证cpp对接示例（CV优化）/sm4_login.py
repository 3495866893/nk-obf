#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""SM4-CBC(动态IV+HMAC-SM3) 完整登录联调
密文 = Base64( 16B IV + SM4-CBC( 8B随机 + 4B时间戳大端 + 业务参数 ) + 32B HMAC-SM3(key, IV+密文) )
"""
import base64, hashlib, json, os, struct, subprocess, time
from urllib.parse import quote
from gmssl.sm4 import CryptSM4, SM4_ENCRYPT, SM4_DECRYPT
from gmssl import sm3, func

BASE = "https://yz.dujiuyue.cn/api.php"
APPID = "11054"
APPKEY = "y4sH2w11imYIx1K4"
KEY = b"tYHhrtawMQIdR5Cx"
KAMI = "2iPMrvyhqH"
MC = hashlib.md5(b"GenericCPUIntelX00000000").hexdigest()


def http(url, body=None):
    cmd = ["curl", "-sk", "-i", "--max-time", "20", url]
    if body is not None:
        cmd += ["--data", body]
    out = subprocess.run(cmd, capture_output=True).stdout.decode("utf-8", "replace")
    head, _, payload = out.partition("\r\n\r\n")
    return (head.split("\r\n", 1)[0].strip() or "NO-RESP"), payload.strip()


def sm3h(b):
    return bytes.fromhex(sm3.sm3_hash(func.bytes_to_list(b)))


def hmac_sm3(key, msg):
    block = 64
    k = key + b"\x00" * (block - len(key)) if len(key) < block else key[:block]
    return sm3h(bytes(x ^ 0x5C for x in k) + sm3h(bytes(x ^ 0x36 for x in k) + msg))


def sm4_enc(payload):
    iv = os.urandom(16)
    n = 16 - len(payload) % 16
    c = CryptSM4()
    c.set_key(KEY, SM4_ENCRYPT)
    ct = c.crypt_cbc(iv, payload + bytes([n]) * n)
    return iv, ct


def sm4_dec(raw):
    iv, ct, tag = raw[:16], raw[16:-32], raw[-32:]
    chk = hmac_sm3(KEY, iv + ct)
    c = CryptSM4()
    c.set_key(KEY, SM4_DECRYPT)
    plain = c.crypt_cbc(iv, ct)
    if plain and 1 <= plain[-1] <= 16:
        plain = plain[:-plain[-1]]
    return plain, (chk == tag)


def pack(plain_str, t):
    return os.urandom(8) + struct.pack(">I", t) + plain_str.encode()


def unpack(plain_bytes):
    body = plain_bytes[12:] if len(plain_bytes) > 12 else plain_bytes
    return json.loads(body.decode("utf-8", "replace"))


def call_api(plain_str, t, extra="", post=True, as_data=True):
    iv, ct = sm4_enc(pack(plain_str, t))
    data = base64.b64encode(iv + ct + hmac_sm3(KEY, iv + ct)).decode()
    if as_data:
        body = "data=" + quote(data, safe="") + extra
        st, rsp = http("%s?api=kmlogon&app=%s" % (BASE, APPID), body if post else None) if post \
            else http("%s?api=kmlogon&app=%s&data=%s" % (BASE, APPID, quote(data, safe="")), None)
    else:
        st, rsp = http("%s?api=kmlogon&app=%s&%s" % (BASE, APPID, plain_str), None)
    return st, rsp


# 1) 取服务器时间
st, rsp = http("%s?api=kmlogon&app=%s" % (BASE, APPID))
raw = base64.b64decode(rsp)
plain, tagok = sm4_dec(raw)
print("[响应] tag校验 %s" % ("OK" if tagok else "FAIL"))
j0 = unpack(plain)
print("[响应明文]", json.dumps(j0, ensure_ascii=False)[:160])
T = int(j0["time"])

# 2) 构造业务明文
sign_src = "kami=%s&user=&markcode=%s&t=%d&%s" % (KAMI, MC, T, APPKEY)
sign = hashlib.md5(sign_src.encode()).hexdigest()
value = "%d%d" % (1000 + int(time.time()) % 99000, T)
plain_with = "kami=%s&user=&markcode=%s&t=%d&sign=%s&value=%s" % (KAMI, MC, T, sign, value)
plain_wo = "kami=%s&user=&markcode=%s&t=%d&sign=%s" % (KAMI, MC, T, sign)
print("\n[T] %d\n[签名原文] %s\n[sign] %s" % (T, sign_src, sign))

CASES = [
    ("A 密文含sign+value, POST", plain_with, "", True, True),
    ("B 密文含sign, POST", plain_wo, "", True, True),
    ("C 密文含sign+value, GET", plain_with, "", False, True),
    ("D 明文直传对照(GET)", plain_with, "", False, False),
]
for name, plain_str, extra, post, as_data in CASES:
    st, rsp = call_api(plain_str, T, extra, post, as_data)
    print("\n--- %s ---\n    %s" % (name, st))
    if not rsp:
        print("    <空响应体>")
        continue
    try:
        raw = base64.b64decode(rsp)
        p, ok = sm4_dec(raw)
        print("    tag=%s  %s" % ("OK" if ok else "FAIL", json.dumps(unpack(p), ensure_ascii=False)[:260]))
    except Exception as e:
        print("    非密文/解析失败 %s: %r" % (type(e).__name__, rsp[:120]))