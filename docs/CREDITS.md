# 来源与致谢

NK 的混淆框架源自开源项目 **Ensia**（一个基于 LLVM 的 C/C++ 代码混淆器），而 Ensia 本身承载了 **OLLVM / Obfuscator-LLVM** 的血统：控制流平坦化、虚假控制流、指令替换、字符串加密这几类经典手段的 IR 实现范式，都来自那条脉络。

NK 是在这套框架上做工程化改造与 bug 修复之后的产物。下面按「上游给了什么 / NK 改了什么」分开写。

---

## 上游：Ensia

| 项 | 内容 |
| --- | --- |
| 项目性质 | 基于 LLVM 的 Pass 插件式 C/C++ 混淆器 |
| 技术血统 | OLLVM / Obfuscator-LLVM 的 IR 混淆思路 |
| 提供的能力 | 字符串加密、常量加密、指令替换、虚假控制流、基本块切分、间接跳转、控制流平坦化、MBA、函数包装、ObjC 支持等 pass 骨架 |
| 接入方式 | `-fpass-plugin` 加载，TOML 配置（`[global]` + `[passes.<name>]`） |
| 上游版权 | 归 Ensia 及其上游作者所有 |

NK 保留了这套框架的**配置模型**（TOML 分段开关、`preset` 预设、`seed` 复现）与**pass 划分方式**，因为它们在多项目复用时的确好用。

## NK 做了什么

### 1. 代码生成修复：Android / TLS

**现象**：`StringEncryption` 生成的全局在 Android 目标上链接失败，报 `.L_v` 临时符号未定义。

**根因**：该 pass 生成全局变量时套用了 TLS 模型，clang 为 TLS 变量生成的 `.L_v` 一类临时符号在链接期被丢弃，而重定位仍指向它——在 `aarch64-linux-android` 的 TLS 描述符规则下必现。

**改法**：生成端改回非 TLS 模型，并按 Android arm64 的 TLS 访问序列重写取址路径。修复后字符串加密在 NDK r29 工具链上可稳定链接。

### 2. 链接修复：COMDAT / `linkonce_odr`

**现象**：开启块切分或平坦化后，链接期报 `relocation refers to a discarded section`。

**根因**：`linkonce_odr` / COMDAT 成员函数被拆成新段后，原段因 COMDAT 语义被丢弃，但重定位条目仍指向原段。

**改法**：在改写前识别 COMDAT 成员，整函数跳过拆段与平坦化，`comdat` 属性原样保留。代价是这类函数（模板实例、inline 函数）的混淆强度较低，换来的是可链接。

### 3. 大 TU 的内存策略

**现象**：单个超大翻译单元（源 > 200 KB，或预处理后 > 800 KB）走 `flattening` 时，LLVM 后端内存占用爆炸，最终产物里出现 `brk` 哨兵指令，运行即 SIGILL。

**改法**：在 pass 入口按 TU 规模判定，超阈值时跳过 `flattening`，其余 pass 照常执行。不调阈值，因为阈值本身就是后端的安全边界。

### 4. 反调试：从 IR 注入点改为外部 C 模块

**原始形态**：反调试逻辑以 IR 形式生成，插在每个函数入口。

**问题**：这段逻辑自己也被 `bcf` / `flattening` 改写，行为不稳定；且每改一次检查项就要改一遍 IR 生成器。

**改法**：IR 层只保留一个外部调用 `call __nk_ad_check()`，真正实现移到 `ci/nk_ad.c`，单独编译成 `nk_ad_extra.o` 后链接。反调试代码从此不参与混淆，可用普通 C 调试器调试，检查项增删不影响 IR 生成。

### 5. `nk_ad` 运行时反调试全套

在「注入点外置」的基础上重写了运行时模块（当前 v11.1），检查项覆盖：

| 类别 | 检查 |
| --- | --- |
| 调试器 | TracerPid 交叉校验、watchdog + `PTRACE_SEIZE` 自检、`/proc/self/stat` trace 状态 |
| 注入 | maps 差分（`r-x` / `rwx` / `memfd` / 无路径 exec）、prologue 编码扫描、uprobe 的 `BRK` 改写 |
| 完整性 | 主模块 + `libc` / `libm` / `libdl` 代码段 FNV 哈希基线（延后采样） |
| 系统调用 | `svc #0` 直调 `openat` / `read` / `close` / `write` / `getdents64`，`nk_svc_ok()` 校验通路 |
| 工具指纹 | Frida（maps / 线程名 / 端口 / `@frida` socket / 落地路径）、Xposed·LSPosed·Zygisk·Riru、QBDI / unicornTrace / substrate |
| 运行时表 | `JavaVM` 前 8 项必须落在 ART 模块内 |
| 硬件断点 | `perf_event_open` + `HW_BREAKPOINT_X`，`ENOSPC` / `EBUSY` 判占 |
| 自保护 | `prctl(PR_SET_DUMPABLE, 0)`、可选 `.text` 写保护 |

命中即 `_exit(97)`；`NK_AD_DEBUG=1` 时输出命中详情。ART 的 JIT 映射与 `libart` 自修改页在实现中被显式放过——这些是合法行为，不放过就没有可用性。

### 6. CI 与双端 GUI 工具链

| 组件 | 作用 |
| --- | --- |
| `ci/build-nk.sh` | 构建 `libNk.so` |
| `ci/build-lucifer.sh` | NDK r29 交叉编译任意工程，自动收集 `-I`、链接静态 libc++、按需追加 `nk_ad_extra.o` |
| `ci/*.toml` | 从 `plain` 到 `max` 的预设谱系，覆盖调试、对比、发布三类场景 |
| `.github/workflows/build-obf.yml` | 参数化云端编译（`src_dir` / `src_files` / `cfg_only` / `out_name` / `anti_debug` / `skip_smoke` / `rebuild_clang`） |
| `tools/NKBuild.py` | Windows 桌面端：选目录 → 选 pass → 一键云端编译 → 产物落 `Download/NKBuild` |
| `android/` | 手机端同功能客户端 |

### 7. 改名

项目由 **Ensia** 全面改名为 **NK**：插件名 `libNk.so`、配置键、文档、CI 参数、GUI 标题一并替换。旧名仅作为来源说明保留在本文档中。

## 上下游脉络

| 代际 | 项目 | 留下的东西 |
| --- | --- | --- |
| 起点 | Obfuscator-LLVM | IR 级混淆的四件套范式：控制流平坦化、虚假控制流、指令替换、字符串加密 |
| 中间 | Ensia | 把上述范式重做成可配置的 `-fpass-plugin` 插件，扩展 pass 集，给出 TOML 配置模型 |
| 现在 | NK | 修可用性 bug（TLS / COMDAT / 大 TU）、反调试外置与重写、CI 与双端 GUI |

三代之间只继承**思路与接口形态**，代码是逐层重写的：NK 的 pass 实现、`nk_ad`、脚本与 GUI 都不来自上游。

## 保留了上游的哪些形状

| 项 | 为什么保留 |
| --- | --- |
| pass 划分粒度 | 一个 pass 一件事，便于单独开合与定位崩溃 |
| TOML 分段配置 | `[global]` + `[passes.<key>]`，人和脚本都好写 |
| `preset` + `seed` | 预设给默认强度，种子保证同一份配置产物可复现 |
| `-fpass-plugin` 接入 | 不侵入构建系统，任何 clang 21 工程一行参数即可 |
| IR 层改写而非源码层 | 与语言、编码风格、宏展开无关 |

## 上位边界

- NK 不重新分发上游源码，也不宣称对上游代码的所有权；本文档只描述「来源是什么、NK 在此基础上做了什么」。
- 上游的 pass 骨架被大量改写，遇到行为差异时以 NK 的实现与本文档为准，不要把上游文档当成本项目的规格说明。
- 第三方组件的许可各自独立，见下表。

## 第三方组件

| 组件 | 用途 | 许可 |
| --- | --- | --- |
| LLVM / clang 21 | 编译器基础设施与 pass 宿主 | Apache-2.0 WITH LLVM-exception |
| Android NDK r29 | Android 交叉编译工具链与 sysroot | 随 NDK 分发 |
| libc++ / libc++abi / libunwind | C++ 静态运行库 | Apache-2.0 WITH LLVM-exception |
| Ensia | 混淆框架来源 | 归上游作者 |

NK 自身代码以 MIT 许可发布，见 [LICENSE](../LICENSE)。
