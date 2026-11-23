#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
nk_gen_baseline_check.py
========================
CI 自检：重新解析 ELF、重算 FNV-1a 64，并与 nk_gen_baseline.py 生成的 .h 逐项比对，
确认「生成器与校验器口径一致」（同一份 .text → 同一组 (off, len, hash, exempt)）。

用法
----
    python nk_gen_baseline_check.py <input.elf> <generated.h>

判定
----
  * 成功打印 `OK n/n`，退出码 0。
  * 任何一项不一致逐条打印（`MISMATCH` / `MISSING`），最后打印 `FAIL x/n`，退出码 1。
  * 解析失败（.h 缺字段、ELF 不可读）打印 `ERROR: ...`，退出码 1。

说明
----
  校验器不关心生成时用了什么 --max-syms / --min-size：它以 .h 里记录的每一
  个 offset 为入口，在 ELF 的 .symtab 里定位对应函数，再独立算一次哈希。
  这样即使生成器换了筛选取样，只要算法口径一致就能对上。
"""

import re
import struct
import sys

FNV_OFFSET_BASIS = 14695981039346656037
FNV_PRIME = 1099511628211
U64 = 0xFFFFFFFFFFFFFFFF

SHT_SYMTAB = 2
STT_FUNC = 2
EM_AARCH64 = 183

EHDR_FMT = "<HHIQQQIHHHHHH"
SHDR_FMT = "<IIQQQQIIQQ"
SYM_FMT = "<IBBHQQ"

EHDR_SIZE = 64
SHDR_SIZE = 64
SYM_SIZE = 24


class CheckError(Exception):
    pass


def fnv1a64(data):
    h = FNV_OFFSET_BASIS
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & U64
    return h


# ---------------------------------------------------------------- ELF

class Elf(object):
    def __init__(self, path):
        self.path = path
        try:
            with open(path, "rb") as f:
                self.buf = f.read()
        except OSError as exc:
            raise CheckError("打开 ELF 失败 %s: %s" % (path, exc))
        if len(self.buf) < EHDR_SIZE:
            raise CheckError("文件长度不足 64 字节: %s" % path)
        if self.buf[:4] != b"\x7fELF":
            raise CheckError("不是 ELF 文件: %s" % path)
        if self.buf[4] != 2 or self.buf[5] != 1:
            raise CheckError("需要 ELF64 小端（EI_CLASS=%d, EI_DATA=%d）: %s"
                             % (self.buf[4], self.buf[5], path))
        fields = struct.unpack_from(EHDR_FMT, self.buf, 16)
        (self.e_type, self.e_machine, self.e_version, self.e_entry,
         self.e_phoff, self.e_shoff, self.e_flags, self.e_ehsize,
         self.e_phentsize, self.e_phnum, self.e_shentsize,
         self.e_shnum, self.e_shstrndx) = fields
        if self.e_machine != EM_AARCH64:
            raise CheckError("需要 aarch64（e_machine=%d）: %s"
                             % (self.e_machine, path))
        if not self.e_shoff or not self.e_shnum:
            raise CheckError("没有节头表: %s" % path)

        entsize = self.e_shentsize or SHDR_SIZE
        if entsize < SHDR_SIZE:
            raise CheckError("e_shentsize=%d < 64: %s" % (entsize, path))
        end = self.e_shoff + self.e_shnum * entsize
        if end > len(self.buf):
            raise CheckError("节头表越界: %s" % path)
        self.shdrs = []
        for i in range(self.e_shnum):
            base = self.e_shoff + i * entsize
            (name, sh_type, flags, addr, offset, size, link, info,
             align, sh_entsize) = struct.unpack_from(SHDR_FMT, self.buf, base)
            self.shdrs.append({
                "index": i, "name_off": name, "type": sh_type, "flags": flags,
                "addr": addr, "offset": offset, "size": size, "link": link,
                "info": info, "align": align, "entsize": sh_entsize, "name": "",
            })
        if self.e_shstrndx >= len(self.shdrs):
            raise CheckError("e_shstrndx 越界: %s" % path)
        strhdr = self.shdrs[self.e_shstrndx]
        if strhdr["offset"] + strhdr["size"] > len(self.buf):
            raise CheckError("节名表越界: %s" % path)
        names = self.buf[strhdr["offset"]:strhdr["offset"] + strhdr["size"]]
        for sh in self.shdrs:
            noff = sh["name_off"]
            if noff >= len(names):
                continue
            nul = names.find(b"\x00", noff)
            if nul < 0:
                nul = len(names)
            sh["name"] = names[noff:nul].decode("utf-8", "replace")

    def section(self, name):
        for sh in self.shdrs:
            if sh["name"] == name:
                return sh
        return None

    def symbol_table(self):
        """返回 {st_value: (name, size)}，只收 .text 内的 STT_FUNC。"""
        text = self.section(".text")
        if text is None:
            raise CheckError("缺少 .text: %s" % self.path)
        symtab = self.section(".symtab")
        if symtab is None:
            raise CheckError("缺少 .symtab: %s" % self.path)
        if symtab["type"] != SHT_SYMTAB:
            raise CheckError(".symtab 类型异常（%d）: %s" % (symtab["type"], self.path))
        strtab = self.section(".strtab")
        if strtab is None:
            raise CheckError("缺少 .strtab: %s" % self.path)
        if symtab["offset"] + symtab["size"] > len(self.buf):
            raise CheckError(".symtab 越界: %s" % self.path)
        if strtab["offset"] + strtab["size"] > len(self.buf):
            raise CheckError(".strtab 越界: %s" % self.path)
        strdata = self.buf[strtab["offset"]:strtab["offset"] + strtab["size"]]
        entsize = symtab["entsize"] or SYM_SIZE
        if entsize < SYM_SIZE:
            raise CheckError(".symtab 的 sh_entsize=%d < 24: %s" % (entsize, self.path))
        out = {}
        n = symtab["size"] // entsize
        for i in range(n):
            base = symtab["offset"] + i * entsize
            (st_name, st_info, st_other, st_shndx,
             st_value, st_size) = struct.unpack_from(SYM_FMT, self.buf, base)
            if (st_info & 0xF) != STT_FUNC:
                continue
            if st_shndx != text["index"]:
                continue
            if st_value < text["addr"] or st_value + st_size > text["addr"] + text["size"]:
                continue
            name = ""
            if st_name < len(strdata):
                nul = strdata.find(b"\x00", st_name)
                if nul < 0:
                    nul = len(strdata)
                name = strdata[st_name:nul].decode("utf-8", "replace")
            out[st_value] = (name, st_size)
        return out, text

    def reloc_offsets(self):
        out = []
        for name in (".rela.dyn", ".rela.plt"):
            for sh in self.shdrs:
                if sh["name"] != name:
                    continue
                entsize = sh["entsize"] or 24
                if entsize < 24:
                    raise CheckError("节 %s 的 sh_entsize=%d < 24: %s" % (name, entsize, self.path))
                if sh["size"] == 0:
                    continue
                if sh["offset"] + sh["size"] > len(self.buf):
                    raise CheckError("节 %s 越界: %s" % (name, self.path))
                for i in range(sh["size"] // entsize):
                    out.append(struct.unpack_from("<Q", self.buf, sh["offset"] + i * entsize)[0])
        return out


# ---------------------------------------------------------------- .h 解析

def parse_header(text):
    """抽出 NK_BASE_N / 三个 ULL 数组 / exempt 位图。"""
    m = re.search(r"#define\s+NK_BASE_N\s+(\d+)", text)
    if not m:
        raise CheckError("找不到 #define NK_BASE_N")
    n = int(m.group(1))

    ull = [int(m[:-3], 16) for m in re.findall(r"0x[0-9a-fA-F]+ULL", text)]
    if len(ull) != 3 * n:
        raise CheckError("按 NK_BASE_N=%d 应有 %d 个 ULL 字面量，实际 %d 个"
                         "（off/len/hash 三组）" % (n, 3 * n, len(ull)))
    offs = ull[0:n]
    lens = ull[n:2 * n]
    hashes = ull[2 * n:3 * n]

    key = "nk_base_exempt"
    pos = text.find(key)
    if pos < 0:
        raise CheckError("找不到 nk_base_exempt 数组")
    seg = text[pos:]
    end = seg.find("};")
    if end < 0:
        raise CheckError("nk_base_exempt 数组没有闭合的 };")
    seg = seg[:end]
    bits = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{1,2})", seg)]
    want = (n + 7) // 8
    if len(bits) != want:
        raise CheckError("nk_base_exempt 应有 %d 字节（(N+7)/8），实际 %d 字节"
                         % (want, len(bits)))
    for b in bits:
        if b > 0xFF:
            raise CheckError("nk_base_exempt 出现越界字节 0x%x" % b)
    return n, offs, lens, hashes, bits


def run(argv):
    if len(argv) != 2:
        sys.stderr.write("用法: python nk_gen_baseline_check.py <input.elf> <generated.h>\n")
        return 2
    elf_path, h_path = argv[0], argv[1]

    elf = Elf(elf_path)
    syms, text = elf.symbol_table()
    relocs = elf.reloc_offsets()

    try:
        with open(h_path, "r", encoding="utf-8") as f:
            htext = f.read()
    except OSError as exc:
        raise CheckError("读取 %s 失败: %s" % (h_path, exc))

    n, offs, lens, hashes, bits = parse_header(htext)
    sys.stderr.write("[chk] 头文件条目: %d (NK_BASE_N=%d), exempt 位图 %d 字节\n"
                     % (n, n, len(bits)))
    sys.stderr.write("[chk] .text: addr=0x%x size=%d, 符号表内函数 %d 个, 重定位 %d 条\n"
                     % (text["addr"], text["size"], len(syms), len(relocs)))

    bad = []
    for i in range(n):
        off = offs[i]
        ln = lens[i]
        hv = hashes[i]
        exempt_h = bool(bits[i >> 3] & (1 << (i & 7)))
        tag = "#%d off=0x%x" % (i, off)

        if off not in syms:
            bad.append("MISSING  %s: ELF 的 .text 内没有 st_value=0x%x 的 STT_FUNC" % (tag, off))
            continue
        name, size = syms[off]
        tag = "#%d %s off=0x%x" % (i, name or "?", off)
        if size != ln:
            bad.append("MISMATCH %s: len .h=%d ELF st_size=%d" % (tag, ln, size))
            continue

        # 重定位命中判定（与生成器同口径：r_offset 落在 [off, off+len)）
        exempt_now = any(off <= r < off + ln for r in relocs)
        if exempt_now != exempt_h:
            bad.append("MISMATCH %s: exempt .h=%s 实算=%s（该函数体内%s重定位）"
                       % (tag, exempt_h, exempt_now, "有" if exempt_now else "无"))
            continue

        if exempt_h:
            if hv != 0:
                bad.append("MISMATCH %s: 已豁免函数 hash 应为 0，.h 为 0x%x" % (tag, hv))
                continue
        else:
            file_off = off - text["addr"] + text["offset"]
            blob = elf.buf[file_off:file_off + ln]
            if len(blob) != ln:
                bad.append("MISMATCH %s: 从文件读到的字节数 %d != %d" % (tag, len(blob), ln))
                continue
            calc = fnv1a64(blob)
            if calc != hv:
                bad.append("MISMATCH %s: hash .h=0x%016x 实算=0x%016x" % (tag, hv, calc))
                continue

    if bad:
        for line in bad:
            print(line)
        print("FAIL %d/%d" % (n - len(bad), n))
        return 1
    print("OK %d/%d" % (n, n))
    return 0


def main():
    try:
        return run(sys.argv[1:])
    except CheckError as exc:
        sys.stderr.write("ERROR: %s\n" % exc)
        print("FAIL 0/0")
        return 1
    except (struct.error, IndexError, ValueError) as exc:
        sys.stderr.write("ERROR: ELF/.h 结构损坏（%s: %s）\n" % (type(exc).__name__, exc))
        print("FAIL 0/0")
        return 1


if __name__ == "__main__":
    sys.exit(main())
