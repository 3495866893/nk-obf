#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
nk_gen_baseline.py
==================
从 Android aarch64 ELF 可执行文件生成「编译期函数级基线」C 头文件，供运行时
反调试 / 完整性校验模块逐段比对（NK_BASE_OFF / NK_BASE_LEN / NK_BASE_HASH /
NK_BASE_EXEMPT）。

用法
----
    python nk_gen_baseline.py <input.elf> <out.h> [--max-syms N] [--min-size B]

口径
----
  * 只依赖标准库；ELF64 / aarch64 / little-endian 的解析全部用 struct 手写。
  * 取 STT_FUNC 且 st_shndx 指向 .text、st_size >= --min-size 的符号，按 st_value 升序。
  * 哈希 = FNV-1a 64（basis 14695981039346656037，prime 1099511628211），
    输入是该函数在 .text 文件镜像中的字节：文件偏移 = st_value - sh_addr + sh_offset。
  * 重定位豁免：若 .rela.dyn / .rela.plt 里存在 r_offset 落在
    [st_value, st_value + st_size) 区间内，该函数不写哈希（写 0），只在
    nk_base_exempt 位图里置 1。装载器会在这些位置做重定位改写，哈希必然对不上。
  * 超过 --max-syms 时按函数体大小从大到小取前 N 个，输出仍按 st_value 升序。

必须在「最终形态」ELF 上执行：链接、混淆、strip、资源重打包之后再跑，否则
.text 任何后续改动都会让基线整体失效。
"""

import argparse
import os
import struct
import sys

# ---------------------------------------------------------------- 常量

FNV_OFFSET_BASIS = 14695981039346656037
FNV_PRIME = 1099511628211
U64 = 0xFFFFFFFFFFFFFFFF

SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_RELA = 4

SHF_EXECINSTR = 0x4
STT_FUNC = 2
EM_AARCH64 = 183

EHDR_FMT = "<HHIQQQIHHHHHH"   # 从偏移 16 开始，共 48 字节
SHDR_FMT = "<IIQQQQIIQQ"      # 64 字节
SYM_FMT = "<IBBHQQ"           # 24 字节
RELA_FMT = "<QQq"             # 24 字节

EHDR_SIZE = 64
SHDR_SIZE = 64
SYM_SIZE = 24
RELA_SIZE = 24


class ElfError(Exception):
    """输入文件不符合预期时抛出，main 捕获后打印简洁错误并 exit 1。"""


def fnv1a64(data):
    """FNV-1a 64bit。"""
    h = FNV_OFFSET_BASIS
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & U64
    return h


# ---------------------------------------------------------------- ELF 解析

class Elf(object):
    """最小可用的 ELF64-aarch64 只读视图。"""

    def __init__(self, path):
        self.path = path
        try:
            with open(path, "rb") as f:
                self.buf = f.read()
        except OSError as exc:
            raise ElfError("打开输入文件失败 %s: %s" % (path, exc))
        if len(self.buf) < EHDR_SIZE:
            raise ElfError("文件长度 %d < ELF64 头 64 字节: %s" % (len(self.buf), path))
        self._parse_ehdr()
        self._parse_shdrs()

    # -- 头

    def _parse_ehdr(self):
        b = self.buf
        if b[:4] != b"\x7fELF":
            raise ElfError("不是 ELF 文件（magic=%r）: %s" % (b[:4], self.path))
        ei_class = b[4]
        ei_data = b[5]
        if ei_class not in (1, 2):
            raise ElfError("EI_CLASS 非法（%d）: %s" % (ei_class, self.path))
        if ei_class != 2:
            raise ElfError("需要 ELF64（EI_CLASS=2），实际 EI_CLASS=%d: %s" % (ei_class, self.path))
        if ei_data != 1:
            raise ElfError("需要小端（EI_DATA=1），实际 EI_DATA=%d: %s" % (ei_data, self.path))
        fields = struct.unpack_from(EHDR_FMT, b, 16)
        (self.e_type, self.e_machine, self.e_version, self.e_entry,
         self.e_phoff, self.e_shoff, self.e_flags, self.e_ehsize,
         self.e_phentsize, self.e_phnum, self.e_shentsize,
         self.e_shnum, self.e_shstrndx) = fields
        if self.e_machine != EM_AARCH64:
            raise ElfError("需要 aarch64（e_machine=%d），实际 e_machine=%d: %s"
                           % (EM_AARCH64, self.e_machine, self.path))
        if self.e_shoff == 0 or self.e_shnum == 0:
            raise ElfError("没有节头表（e_shoff=%d, e_shnum=%d）: %s"
                           % (self.e_shoff, self.e_shnum, self.path))

    # -- 节头

    def _parse_shdrs(self):
        entsize = self.e_shentsize or SHDR_SIZE
        if entsize < SHDR_SIZE:
            raise ElfError("e_shentsize=%d < 64，节头表不可解析: %s" % (entsize, self.path))
        end = self.e_shoff + self.e_shnum * entsize
        if self.e_shoff < 0 or end > len(self.buf):
            raise ElfError("节头表越界（e_shoff=%d, e_shnum=%d, entsize=%d, filesize=%d）: %s"
                           % (self.e_shoff, self.e_shnum, entsize, len(self.buf), self.path))
        self.shdrs = []
        for i in range(self.e_shnum):
            base = self.e_shoff + i * entsize
            (name, sh_type, flags, addr, offset, size, link, info,
             align, sh_entsize) = struct.unpack_from(SHDR_FMT, self.buf, base)
            self.shdrs.append({
                "index": i, "name_off": name, "type": sh_type, "flags": flags,
                "addr": addr, "offset": offset, "size": size, "link": link,
                "info": info, "align": align, "entsize": sh_entsize,
            })
        # 节名
        if self.e_shstrndx >= len(self.shdrs):
            raise ElfError("e_shstrndx=%d 超出节头数量 %d: %s"
                           % (self.e_shstrndx, len(self.shdrs), self.path))
        strhdr = self.shdrs[self.e_shstrndx]
        if strhdr["offset"] + strhdr["size"] > len(self.buf):
            raise ElfError("节名表越界（offset=%d, size=%d）: %s"
                           % (strhdr["offset"], strhdr["size"], self.path))
        names = self.buf[strhdr["offset"]:strhdr["offset"] + strhdr["size"]]
        for sh in self.shdrs:
            noff = sh["name_off"]
            if noff >= len(names):
                sh["name"] = ""
                continue
            nul = names.find(b"\x00", noff)
            if nul < 0:
                nul = len(names)
            sh["name"] = names[noff:nul].decode("utf-8", "replace")

    # -- 访问

    def section(self, name):
        for sh in self.shdrs:
            if sh["name"] == name:
                return sh
        return None

    def sections(self, name):
        return [sh for sh in self.shdrs if sh["name"] == name]

    def addrs(self, name):
        for sh in self.shdrs:
            if sh["name"] == name:
                return sh["addr"]
        return 0


def read_reloc_offsets(elf):
    """收集 .rela.dyn / .rela.plt 的 r_offset（虚拟地址）。不存在的节直接跳过。"""
    out = []
    for name in (".rela.dyn", ".rela.plt"):
        for sh in elf.sections(name):
            if sh["type"] != SHT_RELA and sh["type"] != 0:
                continue
            entsize = sh["entsize"] or RELA_SIZE
            if entsize < RELA_SIZE:
                raise ElfError("节 %s 的 sh_entsize=%d < 24: %s" % (name, entsize, elf.path))
            start = sh["offset"]
            end = start + sh["size"]
            if sh["size"] == 0:
                continue
            if end > len(elf.buf):
                raise ElfError("节 %s 越界（offset=%d, size=%d）: %s"
                               % (name, start, sh["size"], elf.path))
            n = sh["size"] // entsize
            for i in range(n):
                r_offset = struct.unpack_from("<Q", elf.buf, start + i * entsize)[0]
                out.append(r_offset)
    return out


def collect_functions(elf, text, min_size):
    """枚举 .text 内 STT_FUNC 且 st_size >= min_size 的符号，按 st_value 升序返回。"""
    symtab = elf.section(".symtab")
    if symtab is None:
        raise ElfError("缺少 .symtab（被 strip 了？请用保留符号表的最终 ELF）: %s" % elf.path)
    strtab = elf.section(".strtab")
    if strtab is None:
        raise ElfError("缺少 .strtab: %s" % elf.path)
    if symtab["type"] != SHT_SYMTAB:
        raise ElfError(".symtab 类型异常（sh_type=%d）: %s" % (symtab["type"], elf.path))
    if symtab["offset"] + symtab["size"] > len(elf.buf):
        raise ElfError(".symtab 越界: %s" % elf.path)
    if strtab["offset"] + strtab["size"] > len(elf.buf):
        raise ElfError(".strtab 越界: %s" % elf.path)

    strdata = elf.buf[strtab["offset"]:strtab["offset"] + strtab["size"]]
    entsize = symtab["entsize"] or SYM_SIZE
    if entsize < SYM_SIZE:
        raise ElfError(".symtab 的 sh_entsize=%d < 24: %s" % (entsize, elf.path))

    total_func = 0
    funcs = []
    n = symtab["size"] // entsize
    for i in range(n):
        base = symtab["offset"] + i * entsize
        (st_name, st_info, st_other, st_shndx,
         st_value, st_size) = struct.unpack_from(SYM_FMT, elf.buf, base)
        if (st_info & 0xF) != STT_FUNC:
            continue
        if st_shndx != text["index"]:
            continue
        total_func += 1
        name = ""
        if st_name < len(strdata):
            nul = strdata.find(b"\x00", st_name)
            if nul < 0:
                nul = len(strdata)
            name = strdata[st_name:nul].decode("utf-8", "replace")
        if st_size < min_size:
            continue
        # 函数体必须完整落在 .text 文件镜像内
        if st_value < text["addr"] or st_value + st_size > text["addr"] + text["size"]:
            sys.stderr.write("[nk] 警告: 符号 %s (0x%x, size=%d) 越出 .text，已跳过\n"
                             % (name or "?", st_value, st_size))
            continue
        funcs.append({
            "name": name,
            "off": st_value,
            "size": st_size,
            "file_off": st_value - text["addr"] + text["offset"],
        })
    funcs.sort(key=lambda f: f["off"])
    return funcs, total_func


def select_funcs(funcs, max_syms):
    """按 st_value 升序的结果里，若超限则取体积最大的 N 个，再按 st_value 升序输出。"""
    if max_syms and max_syms > 0 and len(funcs) > max_syms:
        top = sorted(funcs, key=lambda f: (-f["size"], f["off"]))[:max_syms]
        top.sort(key=lambda f: f["off"])
        return top
    return funcs


# ---------------------------------------------------------------- 输出

def fmt_u64_lines(values, per_line=8, indent="    "):
    """把整数列表排成 C 初始化列表的若干行（不含外层大括号）。"""
    lines = []
    for i in range(0, len(values), per_line):
        chunk = ", ".join("0x%016xULL" % v for v in values[i:i + per_line])
        lines.append(indent + chunk + ("," if i + per_line < len(values) else ""))
    if not lines:
        lines = [indent + "0"]
    return "\n".join(lines)


def fmt_byte_lines(blob, per_line=12, indent="    "):
    lines = []
    for i in range(0, len(blob), per_line):
        chunk = ", ".join("0x%02x" % b for b in blob[i:i + per_line])
        lines.append(indent + chunk + ("," if i + per_line < len(blob) else ""))
    if not lines:
        lines = [indent + "0"]
    return "\n".join(lines)


def emit_header(n, offs, lens, hashes, exempt_bits):
    out = []
    out.append("/* 由 nk_gen_baseline.py 生成，勿手工编辑 */")
    out.append("#ifndef NK_BASELINE_H")
    out.append("#define NK_BASELINE_H")
    out.append("#define NK_BASE_N %d" % n)
    out.append("#define NK_BASE_VERSION 1u")
    out.append("static const unsigned long long nk_base_off[NK_BASE_N]  = {")
    out.append(fmt_u64_lines(offs))
    out.append("};")
    out.append("static const unsigned long long nk_base_len[NK_BASE_N]  = {")
    out.append(fmt_u64_lines(lens))
    out.append("};")
    out.append("static const unsigned long long nk_base_hash[NK_BASE_N] = {")
    out.append(fmt_u64_lines(hashes))
    out.append("};")
    out.append("static const unsigned char      nk_base_exempt[(NK_BASE_N + 7) / 8] = {")
    out.append(fmt_byte_lines(exempt_bits))
    out.append("};")
    out.append("#endif")
    out.append("")
    return "\n".join(out)


# ---------------------------------------------------------------- main

def run(argv):
    ap = argparse.ArgumentParser(
        prog="nk_gen_baseline.py",
        description="为 Android aarch64 ELF 生成编译期函数级基线头文件（FNV-1a 64）。")
    ap.add_argument("elf", help="输入 ELF（最终形态，未 strip 符号）")
    ap.add_argument("out", help="输出的 .h 路径")
    ap.add_argument("--max-syms", type=int, default=512,
                    help="最多取用多少个函数（按大小取前 N，默认 512；<=0 表示不限制）")
    ap.add_argument("--min-size", type=int, default=32,
                    help="函数体最小字节数（默认 32）")
    args = ap.parse_args(argv)

    if args.min_size < 0:
        sys.stderr.write("[nk] 错误: --min-size 不能为负\n")
        return 1
    if not os.path.isfile(args.elf):
        sys.stderr.write("[nk] 错误: 输入文件不存在: %s\n" % args.elf)
        return 1

    elf = Elf(args.elf)
    text = elf.section(".text")
    if text is None:
        raise ElfError("缺少 .text 节: %s" % args.elf)
    if text["size"] == 0:
        raise ElfError(".text 为空: %s" % args.elf)

    reloc_offsets = read_reloc_offsets(elf)
    funcs_all, total_func = collect_functions(elf, text, args.min_size)
    funcs = select_funcs(funcs_all, args.max_syms)

    if not funcs:
        sys.stderr.write("[nk] 错误: 没有满足条件的函数（.text 内 STT_FUNC=%d，"
                         "st_size >= %d 的有 0 个）。检查 --min-size 或符号表。\n"
                         % (total_func, args.min_size))
        return 1

    # 重定位豁免：函数体内只要出现一个 r_offset 就整函数豁免
    if reloc_offsets:
        reloc_sorted = sorted(reloc_offsets)
    else:
        reloc_sorted = []

    offs, lens, hashes = [], [], []
    exempt_bits = bytearray((len(funcs) + 7) // 8)
    n_exempt = 0
    for i, f in enumerate(funcs):
        offs.append(f["off"])
        lens.append(f["size"])
        exempt = False
        if reloc_sorted:
            # 二分找第一个 >= off 的 r_offset
            lo, hi = 0, len(reloc_sorted)
            while lo < hi:
                mid = (lo + hi) // 2
                if reloc_sorted[mid] < f["off"]:
                    lo = mid + 1
                else:
                    hi = mid
            if lo < len(reloc_sorted) and reloc_sorted[lo] < f["off"] + f["size"]:
                exempt = True
        if exempt:
            exempt_bits[i >> 3] |= (1 << (i & 7))
            hashes.append(0)
            n_exempt += 1
        else:
            blob = elf.buf[f["file_off"]:f["file_off"] + f["size"]]
            hashes.append(fnv1a64(blob))

    text_hdr = emit_header(len(funcs), offs, lens, hashes, bytes(exempt_bits))
    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir and not os.path.isdir(out_dir):
        try:
            os.makedirs(out_dir)
        except OSError as exc:
            sys.stderr.write("[nk] 错误: 无法创建输出目录 %s: %s\n" % (out_dir, exc))
            return 1
    try:
        with open(args.out, "w", encoding="utf-8", newline="\n") as f:
            f.write(text_hdr)
    except OSError as exc:
        sys.stderr.write("[nk] 错误: 写输出文件失败 %s: %s\n" % (args.out, exc))
        return 1

    sys.stderr.write("[nk] 输入       : %s\n" % args.elf)
    sys.stderr.write("[nk] .text      : addr=0x%x size=%d (0x%x) file_off=0x%x\n"
                     % (text["addr"], text["size"], text["size"], text["offset"]))
    sys.stderr.write("[nk] 函数总数   : %d  (STT_FUNC 且 st_shndx=.text)\n" % total_func)
    sys.stderr.write("[nk] 满足 min-size(%d) : %d\n" % (args.min_size, len(funcs_all)))
    sys.stderr.write("[nk] 取用       : %d  (max-syms=%s)\n"
                     % (len(funcs), args.max_syms if args.max_syms > 0 else "无限制"))
    sys.stderr.write("[nk] 重定位豁免 : %d  (.rela.dyn/.rela.plt r_offset 命中 %d 条)\n"
                     % (n_exempt, len(reloc_offsets)))
    sys.stderr.write("[nk] .text 大小 : %d 字节\n" % text["size"])
    sys.stderr.write("[nk] 已写出     : %s (%d 项)\n" % (args.out, len(funcs)))
    return 0


def main():
    try:
        return run(sys.argv[1:])
    except ElfError as exc:
        sys.stderr.write("[nk] 错误: %s\n" % exc)
        return 1
    except (struct.error, IndexError) as exc:
        sys.stderr.write("[nk] 错误: ELF 结构损坏，解析中断（%s: %s）\n"
                         % (type(exc).__name__, exc))
        return 1
    except KeyboardInterrupt:
        sys.stderr.write("[nk] 已中断\n")
        return 130


if __name__ == "__main__":
    sys.exit(main())
