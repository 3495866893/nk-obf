# ci/tools — 编译期函数级基线生成器

给运行时反调试 / 完整性模块（`ci/nk_ad.c`）准备的一份「编译期函数级基线」：
把最终 ELF 里每个函数的 `(偏移, 长度, FNV-1a 64 哈希)` 冻成一张 C 表，运行时在
`.text` 里按同样的偏移读同样的长度再算一次，对不上就判定代码被改写 / 被挂钩 /
被断点插桩。

| 文件 | 作用 |
| --- | --- |
| `nk_gen_baseline.py` | 解析 ELF → 枚举函数 → 算哈希 → 生成 `nk_baseline.h` |
| `nk_gen_baseline_check.py` | 独立再算一遍，和 `.h` 逐项比对（CI 自检） |
| `_selftest_baseline.py` | 现造最小 aarch64 ELF，跑通生成 + 校验全链路 |

三个脚本都只用标准库（ELF 解析是 `struct` 手写的 aarch64 little-endian 版），
Windows 下 `python <脚本>` 直接能跑，Python 3.8+。

---

## 1. 生成器 `nk_gen_baseline.py`

```
python nk_gen_baseline.py <input.elf> <out.h> [--max-syms N] [--min-size B]
```

* `--min-size`（默认 `32`）：函数体小于该字节数的符号不参与基线（太小没鉴别力，
  也容易被链接器/对齐填充吞掉）。
* `--max-syms`（默认 `512`）:超过上限时**按函数体积从大到小取前 N 个**（大函数
  被改写的概率更高、鉴别力更强），输出仍按 `st_value` 升序，保证同一份 ELF
  重复生成的 `.h` 完全一致（可做构建产物 diff）。
* 解析口径：
  * 只认 `STT_FUNC` 且 `st_shndx` 指向 `.text` 的符号，且函数体必须完整落在
    `.text` 的**文件镜像**内；
  * 哈希输入是文件镜像字节，文件偏移 = `st_value - sh_addr + sh_offset`
    （不是运行时虚拟地址；`.text` 在文件里的位置和装载地址无关）；
  * FNV-1a 64：basis `14695981039346656037`，prime `1099511628211`。
* stderr 统计：`.text` 的 addr/size/file_off、函数总数、满足 `min-size` 的数量、
  实际取用数、**重定位豁免数**、`.text` 总大小。
* 解析失败（非 ELF、ELF32、大端、非 aarch64、缺节头表、缺 `.symtab`/`.strtab`、
  节越界、一个函数都没选中）都会打印 `[nk] 错误: ...` 并以 **exit code 1** 结束，
  不抛裸 traceback。

### 产物形状（名字固定，运行时按这个名字取）

```c
/* 由 nk_gen_baseline.py 生成，勿手工编辑 */
#ifndef NK_BASELINE_H
#define NK_BASELINE_H
#define NK_BASE_N 2
#define NK_BASE_VERSION 1u
static const unsigned long long nk_base_off[NK_BASE_N]  = {
    0x0000000000400040ULL, 0x0000000000400050ULL
};
static const unsigned long long nk_base_len[NK_BASE_N]  = {
    0x0000000000000010ULL, 0x0000000000000010ULL
};
static const unsigned long long nk_base_hash[NK_BASE_N] = {
    0x7c84dc9477851775ULL, 0x0000000000000000ULL
};
static const unsigned char      nk_base_exempt[(NK_BASE_N + 7) / 8] = {
    0x02
};
#endif
```

* `nk_base_off[i]` 是**运行时虚拟地址**（`st_value`），运行时直接用；
* `nk_base_len[i]` 是字节数；
* `nk_base_hash[i]` 是 FNV-1a 64；**被豁免项为 0**；
* `nk_base_exempt[i >> 3] & (1 << (i & 7))` 为 1 表示第 `i` 项只校验存在性，
  不校验哈希。

---

## 2. 校验器 `nk_gen_baseline_check.py`

```
python nk_gen_baseline_check.py <input.elf> <generated.h>
```

它不依赖生成器的代码路径（ELF 解析和 FNV 都是另写一遍的），把 `.h` 里的每个
`offset` 当作入口去 `.symtab` 里定位函数，独立重算，然后校验四件事：

1. `nk_base_len[i]` 与 ELF 里的 `st_size` 一致；
2. `nk_base_off[i]` 能在 `.text` 里找到一个 `STT_FUNC`；
3. `nk_base_exempt` 的位与「该函数体内是否真有 `r_offset`」一致；
4. 未豁免项的哈希相等，已豁免项哈希为 0；
5. 附带结构检查：`NK_BASE_N` 与三个 ULL 数组长度一致、exempt 位图字节数
   `= (N+7)/8`。

输出二选一：

* `OK n/n` + exit 0；
* 每条不一致打一行 `MISMATCH #i name off=0x...: ...` / `MISSING ...`，
  末尾 `FAIL x/n` + exit 1。

它**不读生成时的 `--max-syms`/`--min-size`**：以 `.h` 里列出的条目为准，
所以换了取样参数也能对上——这正是 CI 要的「生成器与校验器口径一致」。

---

## 3. 构建顺序：必须在「最终形态 ELF」上跑

这一条比别的都重要。基线是 `.text` 字节的指纹，**哈希输入就是最终要装进 APK
的那份字节**。任何在生成之后还会动 `.text` 的步骤，都会让运行时 100% 校验失败：

```
错误顺序（基线必然对不上）：
  clang 编译 → nk_gen_baseline.py → 混淆/插桩/LLVM pass → strip → 打包

正确顺序：
  clang 编译 → 链接 → 混淆 / 控制流平坦化 / 字符串加密 / LLVM pass 全部跑完
             → （如需）仅删除不参与校验的调试节，保留 .symtab/.strtab
             → nk_gen_baseline.py 生成 nk_baseline.h
             → 编译 nk_ad.c（include 这份 .h）
             → 链接进最终 so/可执行 → 打包
```

几个具体坑：

* **不要对已生成的基线再跑 `strip --strip-all`**：`strip` 会删 `.symtab`，
  下次重新生成直接报「缺少 .symtab」。要么 `--strip-debug`（保留符号表），
  要么把「生成基线」和「打包」放在同一份未 strip 的 ELF 上，`.h` 一旦生成就
  冻结。
* **不要在生成后再链接 / 再 patch 函数入口**：链接会重排 `.text`、改变
  `st_value`，`.h` 里的 `nk_base_off` 立刻失效。
* **签名 / v2/v3 签名不改 `.text`**（apksigner 只动 manifest 和签名块），
  可以放在生成之后，但保险起见仍建议放在最后一步。
* **`.text` 里的动态重定位**（`--export-dynamic`、`-fno-pic` 的绝对地址引用、
  链接器为 `_start`/PLT 生成的槽位）会在**装载时**被改写。这类函数的哈希是
  「编译期算的」和「运行时读到的」天然不同——这就是下面那条豁免规则存在的理由。

---

## 4. 为什么重定位豁免不能省

`.rela.dyn` / `.rela.plt` 里每一条 `r_offset` 都意味着：**装载器在这个虚拟地址
上会写一个值**（GOT 槽、绝对地址、R_AARCH64_RELATIVE 的加数等）。如果
`r_offset` 落在某个函数体 `[st_value, st_value + st_size)` 内，那么这个函数
的运行时字节 ≠ 编译期字节，哈希必然不相等。

不豁免的后果，在生产上是非常典型的「开机自杀」：

1. 反调试模块在 `JNI_OnLoad` / `.init_array` 里跑完整性校验；
2. 第一个被校验的函数体内有重定位 → 哈希不匹配 → 判定为「代码被篡改」；
3. 模块自己 `abort()` / 静默退出 / 清空状态 → App 每次冷启动必崩，抓不到日志，
   现场表现为「装完就闪退」，且只在真机（PIE + 重定位生效）上复现，模拟器
   有时反而是好的。

所以工具采用「**整函数豁免**」而不是「跳过那几个字节」：跳过固定字节数需要
精确知道每种重定位写多宽（x86 是 8/4 字节，aarch64 有 `R_AARCH64_ABS64`、
`RELATIVE`、`JUMP_SLOT`、`TLSDESC` 等多种宽度与偏移），算错一点就又变成误报；
而整函数不校验哈希、只校验「偏移处确实有一条可识别的函数序言」，既不会误报，
也不会给攻击者留下「只要改这一个函数」的稳定缺口（相邻函数仍在被校验）。
`nk_base_exempt` 位图就是这个决策的固化形式。

排查手法：如果运行时日志报某个 `index` 的哈希不符，先看
`nk_base_exempt` 那一位是不是 1；是 1 说明生成器认出了重定位，运行时那边
**不应该**再比哈希（很可能是运行时读的是磁盘上的文件而不是内存镜像，或
`NK_BASE_N`/`NK_BASE_VERSION` 不匹配）。

---

## 5. 接进 `ci/build-lucifer.sh`（示例，本次不修改该文件）

思路：混淆产物 `build/lucifer.elf` 出来后，先生成 `.h` 到工作目录，再让它参与
`nk_ad.c` 的编译；`.h` 不进版本库，跟着构建产物走。

```bash
# ---- ci/build-lucifer.sh 片段（示例，插入到「混淆完成」之后、「编译 nk_ad」之前）

TOOLS_DIR="$(cd "$(dirname "$0")/tools" && pwd)"
PY="${PYTHON:-python3}"

FINAL_ELF="${OUT_DIR}/lucifer.elf"      # 已在前面完成链接 + 混淆的最终 ELF
BASE_H="${OUT_DIR}/nk_baseline.h"

if [ ! -f "${FINAL_ELF}" ]; then
  echo "[build] 缺少 ${FINAL_ELF}，基线无法生成" >&2
  exit 1
fi

# 1) 生成基线（--min-size 32 / --max-syms 512 为默认口径；小体积模块可放宽 min-size）
"${PY}" "${TOOLS_DIR}/nk_gen_baseline.py" \
        "${FINAL_ELF}" "${BASE_H}" \
        --min-size 32 --max-syms 512

# 2) 立刻自检，口径不一致就让构建失败（比等到设备上闪退便宜得多）
"${PY}" "${TOOLS_DIR}/nk_gen_baseline_check.py" "${FINAL_ELF}" "${BASE_H}"

# 3) nk_ad.c 编译时把生成的目录加进 include path
#    （nk_ad.c 里是 #include "nk_baseline.h"，不加 -I 会找不到）
CFLAGS="${CFLAGS} -I${OUT_DIR}"
# ... 之后照原样编译 nk_ad.c 并链接进最终产物 ...
```

要点：

* `nk_ad.c` 里 include 的是 `"nk_baseline.h"`（名字固定），所以 `-I` 指向
  `BASE_H` 所在目录即可，不要改文件名；
* 生成与校验挨着放：一旦生成器/校验器口径漂移（比如有人改了 FNV 参数或取符号
  规则），CI 立刻红，而不是上线后才发现所有设备校验失败；
* CI 里建议把 `${BASE_H}` 作为构建产物归档（artifact），方便线上崩溃时比对
  「当时用的是哪一版基线」；`NK_BASE_VERSION` 目前恒为 `1u`，改口径时手动 +1。

---

## 6. 自测

```
cd repo_main/ci/tools
python _selftest_baseline.py
```

它会用 `struct` 现拼两个最小 ELF64-aarch64（`.text` 32 字节 = 16 字节 × 2 个
函数、`.symtab`、`.strtab`、`.shstrtab`），中间产物落在 `_selftest_out/`：

| 用例 | 内容 | 期望 |
| --- | --- | --- |
| A | 无重定位 | 生成器 exit 0；校验器 `OK 2/2`；豁免 0 |
| B | 加一条 `.rela.dyn`，`r_offset` 落在第 2 个函数体内 | 校验器 `OK 2/2`；豁免 1；被豁免项 hash=0 |
| C | 手工篡改 `.h` 里一个 hash | 校验器 `MISMATCH ...` + `FAIL 1/2` + exit 1 |
| D | 输入不是 ELF | 生成器 exit 1 + 一行 `[nk] 错误: ...`（无 traceback） |

成功时最后一行是 `SELFTEST ALL PASS`，退出码 0。

注意自测用的是 `--min-size 16`（函数只有 16 字节，默认 32 会全部被滤掉）；
真实二进制用默认 32 即可。

---

## 7. 排障对照表

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| `缺少 .symtab（被 strip 了？…）` | 对已 strip 的 ELF 生成 | 用未 strip 的最终 ELF，或 `--strip-debug` |
| `需要 aarch64（e_machine=62）` | 拿了 x86_64 的产物 | 用 `--target=aarch64-linux-android` 的 ELF |
| 运行时所有项哈希都不符 | `.h` 生成后 `.text` 又变了（重链接/再混淆/再 patch） | 把生成移到构建链最末端 |
| 只有个别项不符，且设备是 PIE | 该函数体内有重定位但没被认出来 | 检查 `.rela.dyn` 是否被剥（`--strip-all` 会顺带影响），确认 exempt 位 |
| `OK n/n` 但设备仍报篡改 | 运行时读的是磁盘文件（APK 里的压缩/加密副本），不是内存镜像 | 运行时改读内存（`dl_iterate_phdr` 找基址 + `off` 相加） |
| 两次构建 `.h` 不一致 | 两次 `.text` 真不一样（时间戳/随机种子/BUILD_ID 段） | 固定 `SOURCE_DATE_EPOCH`，本项目只摘要函数体，不受 `--build-id` 影响，但混淆 pass 的随机种子必须固定 |
