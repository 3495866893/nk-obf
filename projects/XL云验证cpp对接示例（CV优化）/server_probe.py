#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
XL云验证 —— 服务端协议侦察 / 自检脚本（实测过 yz.dujiuyue.cn app=11054）

结论（已抓包验证）：
  1. 响应体 = Base64( 12字节IV + AES-256-GCM密文 + 16字节tag )，不是 hex！
     密钥 = 后台「AES密钥」，32 字节原样使用。
  2. 响应里的 check = MD5( time + APPKEY + value )，value 缺省时 value=""。
  3. 请求按官方文档：POST  application/x-www-form-urlencoded  data=Base64(同格式密文)
     Base64 里的 + / = 必须 URL 编码，否则 + 会变成空格导致解密失败。
  4. 本机实测 app=11054：ini / notice 正常；kmlogon / heartbeat 只要 data 解出来非空
     就返回 HTTP 500（空串返回 107「数据为空」），属于服务端该接口的故障或独立加密配置
     不一致，客户端无法绕开。

用法：
  python3 server_probe.py                    # 只看响应解密 + check 校验
  python3 server_probe.py --req              # 再跑一遍请求格式矩阵
"""
import base64
import hashlib
import json
import os
import subprocess
import sys
import time
from urllib.parse import quote

try:
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
except ImportError:
    sys.exit("请先 pip install cryptography")

AESKEY = b"mN4bV7cX1zL9kJ2hG5fD8sA0qW3enknb"   # 后台「AES密钥」（32 字节）
APPKEY = "y4sH2w11imYIx1K4"                    # 后台「应用APPKEY」
APPID = "11054"
BASE = "https://yz.dujiuyue.cn/api.php"


# ---------------- 客户端同款加解密 ----------------
def pack(plain, aad=None):
    if isinstance(plain, str):
        plain = plain.encode()
    iv = os.urandom(12)
    return base64.b64encode(iv + AESGCM(AESKEY).encrypt(iv, plain, aad)).decode()


def unpack(raw):
    raw = raw.strip()
    if not raw:
        return None
    if raw.startswith(b"{"):
        return json.loads(raw)          # 未加密直出（appid 不存在时就是这样）
    d = base64.b64decode(raw)
    return json.loads(AESGCM(AESKEY).decrypt(d[:12], d[12:], None))


def md5(s):
    return hashlib.md5(s.encode()).hexdigest()


# ---------------- HTTP ----------------
def http(url, body=None):
    cmd = ["curl", "-sk", "--max-time", "15", "-i", url]
    if body is not None:
        cmd += ["-X", "POST",
                "-H", "Content-Type: application/x-www-form-urlencoded",
                "--data", body]
    p = subprocess.run(cmd, capture_output=True)
    head, _, payload = p.stdout.partition(b"\r\n\r\n")
    status = head.split(b"\r\n")[0].decode(errors="replace").replace("HTTP/1.1 ", "")
    return status, payload


def show(tag, status, payload):
    try:
        j = unpack(payload)
        body = json.dumps(j, ensure_ascii=False)[:150]
    except Exception:
        body = "RAW=" + payload[:60].decode(errors="replace")
    print("  %-26s %-26s %s" % (tag, status, body))


# ---------------- 主流程 ----------------
def probe_responses():
    print("=== 1. GET 接口响应解密（客户端同款算法） ===")
    for api in ("ini", "notice", "heartbeat", "kmlogon"):
        st, pl = http("%s?api=%s&app=%s" % (BASE, api, APPID))
        show(api, st, pl)

    print("\n=== 2. check 字段公式验证 ===")
    for api in ("heartbeat", "kmlogon"):
        st, pl = http("%s?api=%s&app=%s" % (BASE, api, APPID))
        try:
            j = unpack(pl)
        except Exception:
            continue
        t, chk = str(j.get("time")), j.get("check", "")
        print("  %-10s check=%s" % (api, chk))
        print("             md5(time+APPKEY)  =%s  %s" % (md5(t + APPKEY), md5(t + APPKEY) == chk))
        print("             md5(time)         =%s  %s" % (md5(t), md5(t) == chk))


def probe_requests():
    t = str(int(time.time()))
    mk = hashlib.md5(b"dev").hexdigest()
    kami = "A1B2-C3D4-E5F6-G7H8"
    sign = md5("kami=%s&markcode=%s&t=%s&%s" % (kami, mk, t, APPKEY))
    value = md5(t + APPKEY + mk)
    qs = "kami=%s&markcode=%s&t=%s&sign=%s" % (kami, mk, t, sign)

    print("\n=== 3. 请求格式矩阵（POST data=...） ===")
    cases = [
        ("data=空串(已加密)", "kmlogon", pack("")),
        ("data=Q参数字符串", "kmlogon", pack(qs)),
        ("data=Q参数+明文value", "kmlogon", pack(qs), "&value=" + value),
        ("data=JSON", "kmlogon", pack(json.dumps({"kami": kami, "markcode": mk, "t": int(t), "sign": sign}))),
        ("data=明文(不加密)", "kmlogon", qs),
        ("heartbeat data=t=..", "heartbeat", pack("t=" + t)),
        ("heartbeat data=空串", "heartbeat", pack("")),
        ("ini data=多余参数", "ini", pack("t=" + t)),
    ]
    for c in cases:
        tag, api = c[0], c[1]
        body = "data=" + quote(c[2], safe="") + (c[3] if len(c) > 3 else "")
        st, pl = http("%s?api=%s&app=%s" % (BASE, api, APPID), body)
        show(tag, st, pl)


if __name__ == "__main__":
    probe_responses()
    if "--req" in sys.argv:
        probe_requests()
    print("\n完成。若 kmlogon/heartbeat 全是 500，说明服务端该接口有问题（客户端正常解密、加密都对）。")