#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""P-3: 链接后把 .text 分段哈希写进 __nk_base_hash[] / __nk_base_exempt[]。

必须在「混淆完成、链接完成」之后、strip 之前运行：
  1) 混淆在 IR 层完成，链接后的 .text 就是最终内容；
  2) 基线要写进符号表指向的数组，strip 之后符号表就没了。

用法：
    python3 ci/tools/nk_patch_baseline.py <elf> [-n 64]
退出码：0 = 成功或安全跳过；1 = 参数/读写错误。
"""
import struct
import sys

NK_CSEG = 64
SYM_HASH = "__nk_base_hash"
SYM_EXEMPT = "__nk_base_exempt"

PF_X = 0x1
PT_LOAD = 1
SHT_SYMTAB = 2
SHT_RELA = 4
EM_AARCH64 = 183


def fnv1a64(data, h=0xCBF29CE484222325):
    for b in data:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


class Elf:
    def __init__(self, path):
        self.path = path
        self.raw = bytearray(open(path, "rb").read())
        if self.raw[:4] != b"\x7fELF":
            raise ValueError("不是 ELF 文件")
        if self.raw[4] != 2 or self.raw[5] != 1:
            raise ValueError("只支持 ELF64 小端")
        self.e_machine = struct.unpack_from("<H", self.raw, 18)[0]
        self.e_shoff = struct.unpack_from("<Q", self.raw, 0x28)[0]
        self.e_shentsize = struct.unpack_from("<H", self.raw, 0x3A)[0]
        self.e_shnum = struct.unpack_from("<H", self.raw, 0x3C)[0]
        self.e_phoff = struct.unpack_from("<Q", self.raw, 0x20)[0]
        self.e_phentsize = struct.unpack_from("<H", self.raw, 0x36)[0]
        self.e_phnum = struct.unpack_from("<H", self.raw, 0x38)[0]

    def phdrs(self):
        out = []
        for i in range(self.e_phnum):
            off = self.e_phoff + i * self.e_phentsize
            p_type, p_flags = struct.unpack_from("<II", self.raw, off)
            p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = struct.unpack_from(
                "<QQQQQQ", self.raw, off + 8)
            out.append(dict(type=p_type, flags=p_flags, offset=p_offset, vaddr=p_vaddr,
                            filesz=p_filesz, memsz=p_memsz))
        return out

    def shdrs(self):
        out = []
        for i in range(self.e_shnum):
            off = self.e_shoff + i * self.e_shentsize
            name, s_type, flags, addr, offset, size, link, info, align, entsize = struct.unpack_from(
                "<IIQQQQIIQQ", self.raw, off)
            out.append(dict(name=name, type=s_type, flags=flags, addr=addr, offset=offset,
                            size=size, link=link, info=info, align=align, entsize=entsize))
        return out

    def vaddr_to_off(self, vaddr):
        for ph in self.phdrs():
            if ph["type"] != PT_LOAD:
                continue
            if ph["vaddr"] <= vaddr < ph["vaddr"] + ph["filesz"]:
                return ph["offset"] + (vaddr - ph["vaddr"])
        return None

    def symbols(self):
        """返回 {name: (value, size)}，只取 .symtab 里的定义。"""
        out = {}
        shdrs = self.shdrs()
        for sh in shdrs:
            if sh["type"] != SHT_SYMTAB:
                continue
            strtab = shdrs[sh["link"]]
            base = sh["offset"]
            cnt = sh["size"] // 24 if sh["entsize"] == 0 else sh["size"] // sh["entsize"]
            for i in range(cnt):
                off = base + i * 24
                if off + 24 > len(self.raw):
                    break
                st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from(
                    "<IBBHQQ", self.raw, off)
                if not st_name:
                    continue
                s_off = strtab["offset"] + st_name
                end = self.raw.find(b"\x00", s_off)
                name = bytes(self.raw[s_off:end]).decode("utf-8", "replace")
                if st_shndx != 0 and name not in out:
                    out[name] = (st_value, st_size)
        return out

    def rela_targets(self):
        """所有 RELA 的写入目标地址（r_offset），用于豁免判定。"""
        tgts = []
        for sh in self.shdrs():
            if sh["type"] != SHT_RELA:
                continue
            cnt = sh["size"] // 24
            for i in range(cnt):
                off = sh["offset"] + i * 24
                r_offset, r_info, r_addend = struct.unpack_from("<QQq", self.raw, off)
                tgts.append(r_offset)
        return tgts


def main(argv):
    if len(argv) < 2:
        print("用法: nk_patch_baseline.py <elf> [-n 64]")
        return 1
    path = argv[1]
    nseg = NK_CSEG
    if "-n" in argv:
        nseg = int(argv[argv.index("-n") + 1])

    try:
        elf = Elf(path)
    except Exception as e:                                    # noqa: BLE001
        print("[baseline] 跳过：%s" % e)
        return 0

    if elf.e_machine != EM_AARCH64:
        print("[baseline] 跳过：不是 AArch64（e_machine=%d）" % elf.e_machine)
        return 0

    syms = elf.symbols()
    if SYM_HASH not in syms or SYM_EXEMPT not in syms:
        print("[baseline] 跳过：找不到 %s / %s（可能已 strip，或 nk_ad.c 未链接）"
              % (SYM_HASH, SYM_EXEMPT))
        return 0

    phdrs = elf.phdrs()
    text = None
    for ph in phdrs:
        if ph["type"] == PT_LOAD and (ph["flags"] & PF_X) and ph["memsz"] > 0:
            text = ph
            break
    if not text:
        print("[baseline] 跳过：没有可执行 PT_LOAD")
        return 0

    lo, length = text["vaddr"], text["memsz"]
    per = length // nseg
    if per < 256:
        print("[baseline] 跳过：代码段太小（%d 字节）" % length)
        return 0

    rela = elf.rela_targets()
    hashes = []
    exempt = bytearray((nseg + 7) // 8)
    nex = 0
    for i in range(nseg):
        off = lo + i * per
        size = (length - i * per) if i == nseg - 1 else per
        hit = any(off <= r < off + size for r in rela)
        if hit:
            exempt[i // 8] |= 1 << (i % 8)
            hashes.append(0)
            nex += 1
            continue
        foff = elf.vaddr_to_off(off)
        if foff is None or foff + size > len(elf.raw):
            hashes.append(0)
            exempt[i // 8] |= 1 << (i % 8)
            nex += 1
            continue
        hashes.append(fnv1a64(elf.raw[foff:foff + size]))

    h_off = elf.vaddr_to_off(syms[SYM_HASH][0])
    e_off = elf.vaddr_to_off(syms[SYM_EXEMPT][0])
    if h_off is None or e_off is None:
        print("[baseline] 跳过：数组所在段不可寻址")
        return 0
    if syms[SYM_HASH][1] and syms[SYM_HASH][1] < nseg * 8:
        print("[baseline] 警告：%s 只有 %d 字节，需要 %d"
              % (SYM_HASH, syms[SYM_HASH][1], nseg * 8))
    struct.pack_into("<%dQ" % nseg, elf.raw, h_off, *hashes)
    elf.raw[e_off:e_off + len(exempt)] = exempt

    open(path, "wb").write(elf.raw)
    # 回读确认
    chk = open(path, "rb").read()
    vals = struct.unpack_from("<%dQ" % nseg, chk, h_off)
    ok = sum(1 for v in vals if v != 0)
    print("[baseline] 写入 %d 段哈希（%d 段跳过：%d 个带重定位/豁免），代码段 %d 字节，段宽 %d"
          % (ok, nseg - ok, nex, length, per))
    print("[baseline] %s @0x%x  %s @0x%x"
          % (SYM_HASH, h_off, SYM_EXEMPT, e_off))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
