# -*- coding: utf-8 -*-
"""
XL 云验证 · 桌面端客户端（与 xl_verify.h 完全同协议，纯 Python，无第三方依赖）

协议（来自 xl_verify.h / gm.h）：
    plain  = kami=<卡密>&user=<账号>&markcode=<设备码>&t=<秒>&sign=<md5>&value=<value>
    sign   = md5("kami=" + 卡密 + "&user=" + 账号 + "&markcode=" + 设备码 +
                 "&t=" + 秒 + "&" + APPKEY)                 # 小写十六进制
    value  = "%d%u" % (1000 + t % 99000, t)
    data   = Base64( IV(16) + SM4-CBC(GMKEY, IV, 8B随机 + 4B秒级时间戳大端 + plain) + HMAC-SM3(GMKEY, IV||密文) )
    POST https://<host>/api.php?api=kmlogon&app=11054   body=data=<urlencode(data)>

响应：Base64( IV + 密文 + 32B HMAC-SM3 )  →  JSON
    {"code":200,"msg":{...}|"...","time":<秒>,"check":<md5(str(time)+APPKEY+value)>,"vip":<到期秒>}
    校验：code==200；|time-now|<=30；check == md5(str(time)+APPKEY+value)
"""

from __future__ import annotations

import base64
import hashlib
import json
import os
import secrets
import struct
import time
import urllib.parse
import urllib.request

# ------------------------------ 对接参数（与 xl_verify.h 一致） ------------------------------
APPID = "11054"
APPKEY = "y4sH2w11imYIx1K4"
GMKEY = b"tYHhrtawMQIdR5Cx"            # 后台 SM4 密钥（16 字节）
# 直连官方后台（不带 Ed25519 sig，走 check=md5 校验）；连不通时自动退回中转
HOSTS = ["yz.dujiuyue.cn", "xl-relay.vercel.app"]
API_LOGIN = "api.php?api=kmlogon"
API_UNBIND = "api.php?api=kmunmachine"

CONF_DIR = os.path.join(os.environ.get("APPDATA", os.path.expanduser("~")), "NKBuild")
DEVICE_FILE = os.path.join(CONF_DIR, ".xlv_dev")


# ============================== SM4（GB/T 32907-2016，纯 Python） ==============================
_SBOX = [
    0xd6, 0x90, 0xe9, 0xfe, 0xcc, 0xe1, 0x3d, 0xb7, 0x16, 0xb6, 0x14, 0xc2, 0x28, 0xfb, 0x2c, 0x05,
    0x2b, 0x67, 0x9a, 0x76, 0x2a, 0xbe, 0x04, 0xc3, 0xaa, 0x44, 0x13, 0x26, 0x49, 0x86, 0x06, 0x99,
    0x9c, 0x42, 0x50, 0xf4, 0x91, 0xef, 0x98, 0x7a, 0x33, 0x54, 0x0b, 0x43, 0xed, 0xcf, 0xac, 0x62,
    0xe4, 0xb3, 0x1c, 0xa9, 0xc9, 0x08, 0xe8, 0x95, 0x80, 0xdf, 0x94, 0xfa, 0x75, 0x8f, 0x3f, 0xa6,
    0x47, 0x07, 0xa7, 0xfc, 0xf3, 0x73, 0x17, 0xba, 0x83, 0x59, 0x3c, 0x19, 0xe6, 0x85, 0x4f, 0xa8,
    0x68, 0x6b, 0x81, 0xb2, 0x71, 0x64, 0xda, 0x8b, 0xf8, 0xeb, 0x0f, 0x4b, 0x70, 0x56, 0x9d, 0x35,
    0x1e, 0x24, 0x0e, 0x5e, 0x63, 0x58, 0xd1, 0xa2, 0x25, 0x22, 0x7c, 0x3b, 0x01, 0x21, 0x78, 0x87,
    0xd4, 0x00, 0x46, 0x57, 0x9f, 0xd3, 0x27, 0x52, 0x4c, 0x36, 0x02, 0xe7, 0xa0, 0xc4, 0xc8, 0x9e,
    0xea, 0xbf, 0x8a, 0xd2, 0x40, 0xc7, 0x38, 0xb5, 0xa3, 0xf7, 0xf2, 0xce, 0xf9, 0x61, 0x15, 0xa1,
    0xe0, 0xae, 0x5d, 0xa4, 0x9b, 0x34, 0x1a, 0x55, 0xad, 0x93, 0x32, 0x30, 0xf5, 0x8c, 0xb1, 0xe3,
    0x1d, 0xf6, 0xe2, 0x2e, 0x82, 0x66, 0xca, 0x60, 0xc0, 0x29, 0x23, 0xab, 0x0d, 0x53, 0x4e, 0x6f,
    0xd5, 0xdb, 0x37, 0x45, 0xde, 0xfd, 0x8e, 0x2f, 0x03, 0xff, 0x6a, 0x72, 0x6d, 0x6c, 0x5b, 0x51,
    0x8d, 0x1b, 0xaf, 0x92, 0xbb, 0xdd, 0xbc, 0x7f, 0x11, 0xd9, 0x5c, 0x41, 0x1f, 0x10, 0x5a, 0xd8,
    0x0a, 0xc1, 0x31, 0x88, 0xa5, 0xcd, 0x7b, 0xbd, 0x2d, 0x74, 0xd0, 0x12, 0xb8, 0xe5, 0xb4, 0xb0,
    0x89, 0x69, 0x97, 0x4a, 0x0c, 0x96, 0x77, 0x7e, 0x65, 0xb9, 0xf1, 0x09, 0xc5, 0x6e, 0xc6, 0x84,
    0x18, 0xf0, 0x7d, 0xec, 0x3a, 0xdc, 0x4d, 0x20, 0x79, 0xee, 0x5f, 0x3e, 0xd7, 0xcb, 0x39, 0x48,
]
_FK = [0xA3B1BAC6, 0x56AA3350, 0x677D9197, 0xB27022DC]
_CK = [
    0x00070e15, 0x1c232a31, 0x383f464d, 0x545b6269, 0x70777e85, 0x8c939aa1, 0xa8afb6bd,
    0xc4cbd2d9, 0xe0e7eef5, 0xfc030a11, 0x181f262d, 0x343b4249, 0x50575e65, 0x6c737a81,
    0x888f969d, 0xa4abb2b9, 0xc0c7ced5, 0xdce3eaf1, 0xf8ff060d, 0x141b2229, 0x30373e45,
    0x4c535a61, 0x686f767d, 0x848b9299, 0xa0a7aeb5, 0xbcc3cad1, 0xd8dfe6ed, 0xf4fb0209,
    0x10171e25, 0x2c333a41, 0x484f565d, 0x646b7279,
]


def _rotl(x: int, n: int) -> int:
    n &= 31
    return ((x << n) | (x >> (32 - n))) & 0xFFFFFFFF


def _tau(a: int) -> int:
    return ((_SBOX[(a >> 24) & 0xFF] << 24) | (_SBOX[(a >> 16) & 0xFF] << 16)
            | (_SBOX[(a >> 8) & 0xFF] << 8) | _SBOX[a & 0xFF])


def _t(a: int, key: bool) -> int:
    b = _tau(a)
    if key:
        return b ^ _rotl(b, 13) ^ _rotl(b, 23)
    return b ^ _rotl(b, 2) ^ _rotl(b, 10) ^ _rotl(b, 18) ^ _rotl(b, 24)


class SM4:
    """单块 ECB 加解密；CBC 在外层拼。"""

    def __init__(self, key: bytes):
        if len(key) != 16:
            raise ValueError("SM4 密钥必须 16 字节")
        k = list(struct.unpack(">4I", key))
        for i in range(4):
            k[i] ^= _FK[i]
        self.rk = []
        for i in range(32):
            x = k[i] ^ _t(k[i + 1] ^ k[i + 2] ^ k[i + 3] ^ _CK[i], True)
            k.append(x)
            self.rk.append(x)

    def encrypt_block(self, blk: bytes) -> bytes:
        x = list(struct.unpack(">4I", blk))
        for i in range(32):
            x.append(x[i] ^ _t(x[i + 1] ^ x[i + 2] ^ x[i + 3] ^ self.rk[i], False))
        return struct.pack(">4I", x[35], x[34], x[33], x[32])

    def decrypt_block(self, blk: bytes) -> bytes:
        x = list(struct.unpack(">4I", blk))
        for i in range(32):
            x.append(x[i] ^ _t(x[i + 1] ^ x[i + 2] ^ x[i + 3] ^ self.rk[31 - i], False))
        return struct.pack(">4I", x[35], x[34], x[33], x[32])


def _xor16(a: bytes, b: bytes) -> bytes:
    return bytes(x ^ y for x, y in zip(a, b))


def sm4_cbc_encrypt(key: bytes, iv: bytes, data: bytes) -> bytes:
    c = SM4(key)
    prev, out = iv, bytearray()
    for i in range(0, len(data), 16):
        blk = _xor16(data[i:i + 16], prev)
        prev = c.encrypt_block(blk)
        out += prev
    return bytes(out)


def sm4_cbc_decrypt(key: bytes, iv: bytes, data: bytes) -> bytes:
    c = SM4(key)
    prev, out = iv, bytearray()
    for i in range(0, len(data), 16):
        blk = data[i:i + 16]
        out += _xor16(c.decrypt_block(blk), prev)
        prev = blk
    return bytes(out)


# ============================== SM3 / HMAC-SM3 ==============================
_SM3_IV = [0x7380166f, 0x4914b2b9, 0x172442d7, 0xda8a0600,
           0xa96f30bc, 0x163138aa, 0xe38dee4d, 0xb0fb0e4e]


def _sm3_p0(x):
    return x ^ _rotl(x, 9) ^ _rotl(x, 17)


def _sm3_p1(x):
    return x ^ _rotl(x, 15) ^ _rotl(x, 23)


def sm3(data: bytes) -> bytes:
    v = list(_SM3_IV)
    bits = len(data) * 8
    msg = data + b"\x80" + b"\x00" * ((56 - (len(data) + 1) % 64) % 64) + struct.pack(">Q", bits)
    for off in range(0, len(msg), 64):
        blk = msg[off:off + 64]
        w = list(struct.unpack(">16I", blk)) + [0] * 52
        for j in range(16, 68):
            w[j] = (_sm3_p1(w[j - 16] ^ w[j - 9] ^ _rotl(w[j - 3], 15))
                    ^ _rotl(w[j - 13], 7) ^ w[j - 6])
        w1 = [w[j] ^ w[j + 4] for j in range(64)]
        a, b, c, d, e, f, g, h = v
        for j in range(64):
            tj = 0x79cc4519 if j < 16 else 0x7a879d8a
            ss1 = _rotl((_rotl(a, 12) + e + _rotl(tj, j & 31)) & 0xFFFFFFFF, 7)
            ss2 = ss1 ^ _rotl(a, 12)
            ff = (a ^ b ^ c) if j < 16 else ((a & b) | (a & c) | (b & c))
            gg = (e ^ f ^ g) if j < 16 else ((e & f) | ((~e & 0xFFFFFFFF) & g))
            tt1 = (ff + d + ss2 + w1[j]) & 0xFFFFFFFF
            tt2 = (gg + h + ss1 + w[j]) & 0xFFFFFFFF
            d, c, b, a = c, _rotl(b, 9), a, tt1
            h, g, f, e = g, _rotl(f, 19), e, _sm3_p0(tt2)
        v = [x ^ y for x, y in zip(v, [a, b, c, d, e, f, g, h])]
    return struct.pack(">8I", *v)


def hmac_sm3(key: bytes, msg: bytes) -> bytes:
    if len(key) > 64:
        key = sm3(key)
    k = key + b"\x00" * (64 - len(key))
    return sm3(bytes(x ^ 0x5c for x in k) + sm3(bytes(x ^ 0x36 for x in k) + msg))


# ============================== 传输封装（与 GmWrap / GmUnwrap 同格式） ==============================
def gm_wrap(plain: bytes, key: bytes = GMKEY) -> str:
    t = int(time.time()) & 0xFFFFFFFF
    body = secrets.token_bytes(8) + struct.pack(">I", t) + plain
    pad = 16 - (len(body) % 16)
    body += bytes([pad]) * pad
    iv = secrets.token_bytes(16)
    k = key[:16]
    ct = sm4_cbc_encrypt(k, iv, body)
    tag = hmac_sm3(k, iv + ct)
    return base64.b64encode(iv + ct + tag).decode()


def gm_unwrap(b64: str, key: bytes = GMKEY) -> bytes:
    """与 gm.h 的 GmUnwrap 一致：解出明文后丢掉前 12 字节（8 随机 + 4 时间戳）"""
    raw = base64.b64decode(b64)
    if len(raw) < 16 + 16 + 32:
        raise ValueError("响应太短")
    iv, ct, tag = raw[:16], raw[16:-32], raw[-32:]
    if hmac_sm3(key[:16], iv + ct) != tag:
        raise ValueError("HMAC-SM3 校验失败（响应被改过或密钥不对）")
    body = sm4_cbc_decrypt(key[:16], iv, ct)
    pad = body[-1]
    if not (1 <= pad <= 16) or body[-pad:] != bytes([pad]) * pad:
        raise ValueError("填充损坏")
    body = body[:-pad]
    if len(body) < 12:
        raise ValueError("明文太短")
    return body[12:]


# ============================== 设备码（持久化，重装不变） ==============================
def device_code() -> str:
    os.makedirs(CONF_DIR, exist_ok=True)
    try:
        with open(DEVICE_FILE, "r", encoding="utf-8") as f:
            c = f.read().strip()
        if len(c) == 32:
            return c
    except OSError:
        pass
    tb = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    raw = "".join(secrets.choice(tb) for _ in range(10))
    code = hashlib.md5(raw.encode()).hexdigest()
    try:
        with open(DEVICE_FILE, "w", encoding="utf-8") as f:
            f.write(code)
    except OSError:
        pass
    return code


# ============================== HTTP ==============================
def _http_post(path_query: str, body: str) -> tuple[str, str]:
    """按 HOSTS 顺序尝试；直连不通就退回中转。返回 (响应体, 错误文案)"""
    data = body.encode()
    last = "网络问题"
    for host in HOSTS:
        req = urllib.request.Request(f"https://{host}/{path_query}", data=data,
                                     method="POST", headers={
            "Content-Type": "application/x-www-form-urlencoded",
            "User-Agent": "NKBuild",
            "Accept": "*/*",
            "Connection": "close",
        })
        try:
            with urllib.request.urlopen(req, timeout=15) as r:
                raw = r.read()
        except Exception as e:                               # noqa: BLE001
            msg = str(e)
            last = "网络问题"
            for code in ("500", "502", "503", "504", "520", "521", "522", "523", "524"):
                if code in msg:
                    last = "服务器问题"
            continue
        text = raw.decode("utf-8", "replace")
        return "".join(ch for ch in text if ch not in "\r\n\t "), ""
    return "", last


def _md5_hex(s: str) -> str:
    return hashlib.md5(s.encode()).hexdigest()


def _norm_ts(t: int) -> int:
    if t > 100000000000000:
        return t // 1000000
    if t > 100000000000:
        return t // 1000
    return t


# ============================== 登录 / 解绑 ==============================
def login(kami: str, user: str = "") -> dict:
    """返回 {ok, msg, expiry, vip, code}"""
    kami = (kami or "").strip()
    if not kami:
        return {"ok": False, "msg": "卡密不能为空", "expiry": "", "vip": 0, "code": 0}
    imei = device_code()
    t = int(time.time())
    sign = _md5_hex(f"kami={kami}&user={user}&markcode={imei}&t={t}&{APPKEY}")
    value = f"{1000 + t % 99000}{t}"
    plain = f"kami={kami}&user={user}&markcode={imei}&t={t}&sign={sign}&value={value}"
    body = "data=" + urllib.parse.quote(gm_wrap(plain.encode()), safe="")

    resp, neterr = _http_post(f"{API_LOGIN}&app={APPID}", body)
    if not resp:
        return {"ok": False, "msg": neterr or "网络问题", "expiry": "", "vip": 0, "code": 0}

    # 明文响应一律拒绝：只认带正确 HMAC 的 Base64 密文
    if len(resp) < 16 or sum(c.isalnum() or c in "+/=" for c in resp) < len(resp) * 0.95:
        return {"ok": False, "msg": "响应格式异常", "expiry": "", "vip": 0, "code": 0}
    try:
        plain_text = gm_unwrap(resp).decode("utf-8", "replace")
        root = json.loads(plain_text)
    except Exception:                                        # noqa: BLE001
        return {"ok": False, "msg": "响应解密失败", "expiry": "", "vip": 0, "code": 0}

    code = int(root.get("code") or 0)
    if code != 200:
        return {"ok": False, "msg": str(code), "expiry": "", "vip": 0, "code": code}

    st = _norm_ts(int(root.get("time") or 0))
    now = int(time.time())
    if st <= 0 or abs(st - now) > 30:
        return {"ok": False, "msg": "时间戳异常", "expiry": "", "vip": 0, "code": 401}
    check = str(root.get("check") or "").lower()
    if check != _md5_hex(f"{st}{APPKEY}{value}"):
        return {"ok": False, "msg": "校验未通过", "expiry": "", "vip": 0, "code": 401}

    vip = 0
    msgobj = root.get("msg")
    for k in ("vip", "endtime", "expire", "expiretime"):
        v = _norm_ts(int(root.get(k) or 0))
        if v <= 0 and isinstance(msgobj, dict):
            v = _norm_ts(int(msgobj.get(k) or 0))
        if v > 0:
            vip = v
            break
    if vip > 0:
        expiry = time.strftime("%Y-%m-%d %H:%M", time.localtime(vip))
        if vip - now < 0:
            return {"ok": False, "msg": "卡密已过期", "expiry": expiry, "vip": vip, "code": 200}
    else:
        expiry = "永久/未返回"
    return {"ok": True, "msg": "登录成功", "expiry": expiry, "vip": vip, "code": 200}


def unbind(kami: str) -> dict:
    kami = (kami or "").strip()
    if not kami:
        return {"ok": False, "msg": "没有可解绑的卡密"}
    imei = device_code()
    t = int(time.time())
    sign = _md5_hex(f"kami={kami}&user=&markcode={imei}&t={t}&{APPKEY}")
    plain = f"kami={kami}&user=&markcode={imei}&t={t}&sign={sign}"
    body = "data=" + urllib.parse.quote(gm_wrap(plain.encode()), safe="")
    resp, neterr = _http_post(f"{API_UNBIND}&app={APPID}", body)
    if not resp:
        return {"ok": False, "msg": neterr or "网络问题"}
    try:
        root = json.loads(gm_unwrap(resp).decode("utf-8", "replace"))
    except Exception:                                        # noqa: BLE001
        return {"ok": False, "msg": "响应解密失败"}
    code = int(root.get("code") or 0)
    return {"ok": code == 200, "msg": "解绑完成" if code == 200 else str(code)}


if __name__ == "__main__":
    import sys

    km = sys.argv[1] if len(sys.argv) > 1 else ""
    print("设备码:", device_code())
    print("登录结果:", login(km))
