#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
_selftest_baseline.py
=====================
产物自测：用 Python 拼一个最小 aarch64 ELF64（.text 里 16 字节 × 2 个函数 +
.symtab + .strtab，默认无重定位），跑 nk_gen_baseline.py，再跑
nk_gen_baseline_check.py，确认 `OK 2/2`。

附加用例：
  B. 同一个 ELF 加一条 .rela.dyn（r_offset 落在第 2 个函数体内）→
     该函数必须被豁免（exempt 计数 1），校验器仍然 `OK 2/2`。
  C. 篡改 .h 里一个 hash → 校验器必须 FAIL、退出码 1（证明校验器真的在校验）。
  D. 输入不是 ELF → 生成器必须 exit 1 且输出一行可读错误（不抛裸 traceback）。

用法：
    python _selftest_baseline.py            # 在本目录生成 _selftest_out/ 中间产物
    python _selftest_baseline.py --keep     # 同上（保留中间文件，本来就是保留的）
"""

import os
import re
import struct
import subprocess
import sys

# Windows 控制台默认 cp936，中文统计行容易炸 UnicodeEncodeError；统一按 UTF-8 输出。
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(HERE, "nk_gen_baseline.py")
CHK = os.path.join(HERE, "nk_gen_baseline_check.py")
OUTDIR = os.path.join(HERE, "_selftest_out")

EHDR_SIZE = 64
SHDR_SIZE = 64
SYM_SIZE = 24
RELA_SIZE = 24

TEXT_VADDR = 0x400000 + 0x40


def build_elf(text_blob, funcs, relocs=(), text_vaddr=TEXT_VADDR):
    """拼一个可被本工具链解析的最小 ELF64-aarch64。

    text_blob : .text 的原始字节
    funcs     : [(name, off_in_text, size)]
    relocs    : [(r_offset_vaddr, r_info, r_addend)]，非空时生成 .rela.dyn
    """
    has_rela = bool(relocs)
    i_text = 1
    i_symtab = 2
    i_strtab = 3
    i_rela = 4 if has_rela else 0
    i_shstr = 5 if has_rela else 4
    shnum = 6 if has_rela else 5

    # ---- .strtab / .symtab
    strtab = bytearray(b"\x00")
    syms = bytearray(SYM_SIZE)  # sym[0] = 全 0
    for name, off, size in funcs:
        noff = len(strtab)
        strtab += name.encode("utf-8") + b"\x00"
        # st_info = (STB_GLOBAL << 4) | STT_FUNC = 0x12
        syms += struct.pack("<IBBHQQ", noff, 0x12, 0, i_text, text_vaddr + off, size)

    # ---- .rela.dyn
    rela = bytearray()
    for r_off, r_info, r_add in relocs:
        rela += struct.pack("<QQq", r_off, r_info, r_add)

    # ---- .shstrtab
    order = [b".text", b".symtab", b".strtab"]
    if has_rela:
        order.append(b".rela.dyn")
    order.append(b".shstrtab")
    shstr = bytearray(b"\x00")
    name_off = {}
    for nm in order:
        name_off[nm] = len(shstr)
        shstr += nm + b"\x00"

    # ---- 布局
    blob = bytearray(EHDR_SIZE)

    def place(data, align):
        while len(blob) % align:
            blob.append(0)
        off = len(blob)
        blob.extend(data)
        return off

    off_text = place(bytes(text_blob), 4)
    off_strtab = place(bytes(strtab), 1)
    off_symtab = place(bytes(syms), 8)
    off_rela = place(bytes(rela), 8) if has_rela else 0
    off_shstr = place(bytes(shstr), 1)

    # ---- 节头表
    while len(blob) % 8:
        blob.append(0)
    off_shdr = len(blob)

    def shdr(name_off_v, sh_type, flags, addr, offset, size, link, info, align, entsize):
        return struct.pack("<IIQQQQIIQQ", name_off_v, sh_type, flags, addr,
                           offset, size, link, info, align, entsize)

    shdrs = bytearray()
    shdrs += shdr(0, 0, 0, 0, 0, 0, 0, 0, 0, 0)                                   # 0 NULL
    shdrs += shdr(name_off[b".text"], 1, 0x6, text_vaddr, off_text,
                  len(text_blob), 0, 0, 4, 0)                                     # 1 .text
    shdrs += shdr(name_off[b".symtab"], 2, 0, 0, off_symtab, len(syms),
                  i_strtab, 1, 8, SYM_SIZE)                                       # 2 .symtab
    shdrs += shdr(name_off[b".strtab"], 3, 0, 0, off_strtab, len(strtab),
                  0, 0, 1, 0)                                                     # 3 .strtab
    if has_rela:
        shdrs += shdr(name_off[b".rela.dyn"], 4, 0, 0, off_rela, len(rela),
                      i_symtab, i_text, 8, RELA_SIZE)                             # 4 .rela.dyn
    shdrs += shdr(name_off[b".shstrtab"], 3, 0, 0, off_shstr, len(shstr),
                  0, 0, 1, 0)                                                     # .shstrtab
    assert len(shdrs) == shnum * SHDR_SIZE, (len(shdrs), shnum)

    blob.extend(shdrs)

    # ---- ELF 头
    e_ident = b"\x7fELF" + bytes([2, 1, 1, 0]) + b"\x00" * 8
    ehdr = e_ident + struct.pack("<HHIQQQIHHHHHH",
                                 2,            # e_type = ET_EXEC
                                 183,          # e_machine = EM_AARCH64
                                 1,            # e_version
                                 text_vaddr,   # e_entry
                                 0,            # e_phoff
                                 off_shdr,     # e_shoff
                                 0,            # e_flags
                                 64,           # e_ehsize
                                 0,            # e_phentsize
                                 0,            # e_phnum
                                 SHDR_SIZE,    # e_shentsize
                                 shnum,        # e_shnum
                                 i_shstr)      # e_shstrndx
    assert len(ehdr) == 64
    blob[0:64] = ehdr
    return bytes(blob)


# ---------------------------------------------------------------- 运行器

def run(cmd):
    env = os.environ.copy()
    env["PYTHONIOENCODING"] = "utf-8"
    env["PYTHONUTF8"] = "1"
    p = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8",
                       errors="replace", env=env)
    return p.returncode, p.stdout or "", p.stderr or ""


def one_case(tag, elf_path, h_path, min_size=16):
    rc, out, err = run([sys.executable, GEN, elf_path, h_path, "--min-size", str(min_size)])
    print("  [gen] rc=%d" % rc)
    for line in err.strip().splitlines():
        print("        " + line)
    if rc != 0:
        raise SystemExit("[FAIL] %s: 生成器退出码 %d" % (tag, rc))
    if not os.path.isfile(h_path):
        raise SystemExit("[FAIL] %s: 没有生成 %s" % (tag, h_path))

    rc2, out2, err2 = run([sys.executable, CHK, elf_path, h_path])
    print("  [chk] rc=%d  stdout=%s" % (rc2, out2.strip()))
    for line in err2.strip().splitlines():
        print("        " + line)
    return rc, err, rc2, out2, err2, h_path


def exempt_count(err):
    m = re.search(r"重定位豁免\s*:\s*(\d+)", err)
    return int(m.group(1)) if m else -1


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    fails = []

    # 用例 A：无重定位，两个 16 字节函数
    text = bytes(range(0x20))  # 0x00..0x1f，正好 32 字节 = 16 + 16
    funcs = [("nk_a", 0, 16), ("nk_b", 16, 16)]
    elf_a = build_elf(text, funcs)
    pa = os.path.join(OUTDIR, "selftest_a.elf")
    with open(pa, "wb") as f:
        f.write(elf_a)
    print("== 用例 A：最小 ELF（.text 32B，2 个函数，无重定位）-> %s" % pa)
    ha = os.path.join(OUTDIR, "nk_baseline_a.h")
    rc, err, rc2, out2, err2, ha = one_case("A", pa, ha)
    ex_a = exempt_count(err)
    if out2.strip() != "OK 2/2":
        fails.append("A: 期望 'OK 2/2'，实际 %r" % out2.strip())
    if ex_a != 0:
        fails.append("A: 期望豁免 0，实际 %d" % ex_a)
    htext = open(ha, encoding="utf-8").read()
    for token in ("#ifndef NK_BASELINE_H", "#define NK_BASE_N 2", "#define NK_BASE_VERSION 1u",
                  "nk_base_off[NK_BASE_N]", "nk_base_len[NK_BASE_N]",
                  "nk_base_hash[NK_BASE_N]", "nk_base_exempt[(NK_BASE_N + 7) / 8]"):
        if token not in htext:
            fails.append("A: 头文件缺少 %r" % token)

    # 用例 B：加一条 .rela.dyn，r_offset 落在第 2 个函数体内
    relocs = [(TEXT_VADDR + 16 + 4, (0x01 << 32) | 0x403, 0)]
    elf_b = build_elf(text, funcs, relocs)
    pb = os.path.join(OUTDIR, "selftest_b.elf")
    with open(pb, "wb") as f:
        f.write(elf_b)
    print("\n== 用例 B：同一 ELF + .rela.dyn(r_offset=0x%x，落在 nk_b 体内)" % relocs[0][0])
    hb = os.path.join(OUTDIR, "nk_baseline_b.h")
    rc, err, rc2, out2, err2, hb = one_case("B", pb, hb)
    ex_b = exempt_count(err)
    if out2.strip() != "OK 2/2":
        fails.append("B: 期望 'OK 2/2'，实际 %r" % out2.strip())
    if ex_b != 1:
        fails.append("B: 期望豁免 1，实际 %d" % ex_b)
    hb_text = open(hb, encoding="utf-8").read()
    hashes = [int(m[:-3], 16) for m in re.findall(r"0x[0-9a-fA-F]+ULL", hb_text)][4:6]
    if hashes[1] != 0:
        fails.append("B: 被豁免函数的 hash 应为 0，实际 0x%x" % hashes[1])

    # 用例 C：篡改一个 hash，校验器必须失败
    print("\n== 用例 C：篡改 .h 中第 1 个 hash")
    bad = hb_text.replace("%016xULL" % hashes[0], "%016xULL" % (hashes[0] ^ 0xFFFF))
    hc = os.path.join(OUTDIR, "nk_baseline_c_tampered.h")
    with open(hc, "w", encoding="utf-8", newline="\n") as f:
        f.write(bad)
    rc3, out3, err3 = run([sys.executable, CHK, pb, hc])
    print("  [chk] rc=%d  stdout=%s" % (rc3, out3.strip()))
    if rc3 == 0 or "MISMATCH" not in out3:
        fails.append("C: 篡改后校验器应 FAIL，实际 rc=%d out=%r" % (rc3, out3.strip()))

    # 用例 D：非 ELF 输入
    print("\n== 用例 D：非 ELF 输入的错误处理")
    pd = os.path.join(OUTDIR, "not_an_elf.bin")
    with open(pd, "wb") as f:
        f.write(b"not an elf at all" * 8)
    rc4, out4, err4 = run([sys.executable, GEN, pd, os.path.join(OUTDIR, "x.h")])
    print("  [gen] rc=%d  stderr=%s" % (rc4, err4.strip()))
    if rc4 != 1:
        fails.append("D: 期望 exit 1，实际 %d" % rc4)
    if "Traceback" in err4:
        fails.append("D: 不该抛裸 traceback")
    if "错误" not in err4:
        fails.append("D: 缺少可读错误信息")

    print("\n---- 汇总 ----")
    if fails:
        for f in fails:
            print("  [FAIL] " + f)
        return 1
    print("  [PASS] 用例 A：OK 2/2，豁免 0")
    print("  [PASS] 用例 B：OK 2/2，豁免 1（.rela.dyn 命中 nk_b）")
    print("  [PASS] 用例 C：篡改 hash 后校验器 FAIL（校验器真的在校验）")
    print("  [PASS] 用例 D：非 ELF 输入 exit 1 且一行可读错误")
    print("  SELFTEST ALL PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
