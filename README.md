# NK

> 基于 **LLVM 21** 的 IR 层代码混淆加固 Pass 插件。一个 `libNk.so`，一条 `-fpass-plugin` 参数。

NK 是一个 LLVM Pass 插件（产物 `libNk.so`）。把它加载进 clang 之后，C/C++ 源码在 **LLVM IR 层**被改写：字符串与常量加密、控制流平坦化、虚假控制流、指令替换、间接跳转、函数包装，最终由后端生成 Android / Windows 原生二进制。运行时另有独立的 C 模块 `nk_ad` 负责反调试与反注入，不与混淆 IR 混编。

NK 基于一个开源 LLVM 混淆框架起步，之后做了大量工程化改造与 bug 修复：Android/TLS 代码生成、COMDAT 链接、大 TU 内存策略、反调试外置模块、CI 与双端 GUI。来源与改动清单见 [docs/CREDITS.md](docs/CREDITS.md)。

**本仓库为私有仓库，未对外开源分发。**

---

## 目录导航

| 文档 | 内容 |
| --- | --- |
| [docs/FEATURES.md](docs/FEATURES.md) | 16 个 pass 逐个说明：作用、参数、默认状态、代价 |
| [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) | 编译器 / 平台 / 语言 / ABI 支持矩阵与已知限制 |
| [docs/USAGE.md](docs/USAGE.md) | 构建插件、单文件编译、NDK 交叉编译、TOML 配置、GUI、CI |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 插件流水线、注入点设计、`nk_ad` 运行时结构、工程约束 |
| [docs/CREDITS.md](docs/CREDITS.md) | 上游来源（OLLVM 血统）与 NK 的改动清单 |
| [docs/CHANGELOG.md](docs/CHANGELOG.md) | 版本记录 v1.0 → 当前 |
| [LICENSE](LICENSE) | MIT，Copyright (c) 2026 nk |

阅读顺序建议：先本文的「它做什么」→ COMPATIBILITY 确认环境 → USAGE 跑通一次 → FEATURES 调参数 → ARCHITECTURE 理解为什么这么设计。

---

## 它做什么

| 层 | 输入 | 处理 | 输出 |
| --- | --- | --- | --- |
| 编译期 | `.c` / `.cpp` 源码 | clang 前端 → LLVM IR → **NK pass 改写 IR** → 后端 | `.o` / `.so` / `.exe` |
| 运行期 | 加固后二进制 | `nk_ad.c` 单独编成 `nk_ad_extra.o` 链入，做自检 | 命中即 `_exit(97)` |

- 加载方式：`clang -fpass-plugin=/path/libNk.so`
- 配置方式：TOML（`[global]` + `[passes.<name>]`），环境变量 `NK=1` 开启、`NK_CONFIG=<toml>` 指定配置文件
- 目标平台：Android `aarch64-linux-android21`（NDK r29）、Windows x86_64，Linux x86_64 同样方式可用

## 30 秒上手

```bash
# 1) 单文件：直接挂插件
clang++ -fpass-plugin=/path/libNk.so -O2 -c a.cpp

# 2) Android NDK：走仓库脚本（NDK r29 + clang 21）
NK=1 NK_CONFIG=ci/tuned.toml \
  bash ci/build-lucifer.sh /opt/android-ndk-r29 /path/plugin-build out_name
```

产出的 `.so` 即为加固后的 Android 原生库。完整步骤与全部开关见 [docs/USAGE.md](docs/USAGE.md)。

---

## 16 个 Pass

| # | Pass | 作用 | 状态 |
| --- | --- | --- | --- |
| 1 | `string_encryption` | 字符串常量加密，运行时解密、用完清零 | 默认开 |
| 2 | `constant_encryption` | 常量全局化 + XOR 拆分 | 默认开 |
| 3 | `substitution` | 等价指令序列替换 | 默认开 |
| 4 | `bcf` | 虚假控制流（不透明谓词 + 假分支） | 默认开 |
| 5 | `split_blocks` | 基本块切分（`splits` / `stack_confusion`） | 默认开 |
| 6 | `indirect_branch` | 跳转表上栈 + 跳转目标加密 | 默认开 |
| 7 | `flattening` | 控制流平坦化（状态机 + 分发器） | 默认开，大 TU 自动跳过 |
| 8 | `anti_debugging` | IR 级反调试注入点（每函数入口 `call __nk_ad_check()`） | 默认开 |
| 9 | `anti_hooking` | 反 hook / 完整性基线 | **默认关闭** |
| 10 | `mba` | MBA 位运算混淆 | 实验性 |
| 11 | `chaos_state_machine` | 混沌状态机 | 未充分验证 |
| 12 | `vector_obfuscation` | 向量混淆 | 实验性 |
| 13 | `function_wrapper` | 函数包装 | 实验性 |
| 14 | `function_call_obfuscate` | 调用混淆 | 实验性 |
| 15 | `anti_class_dump` | 针对 ObjC / Java 类结构 | **默认关闭** |
| 16 | `objc` | ObjC 支持 | **默认关闭** |

逐项参数、默认值与体积/耗时代价见 [docs/FEATURES.md](docs/FEATURES.md)。

## 兼容性摘要

| 项 | 要求 |
| --- | --- |
| 编译器 | LLVM / clang **21**（必需，ABI 与 pass API 绑定） |
| 构建插件 | LLVM 21 头文件 + 静态库 |
| Android | NDK r29，目标 triple `aarch64-linux-android21` |
| Windows | x86_64（独立分支） |
| Linux | x86_64，加载方式同上 |
| 语言 | C / C++（C++ 标准库静态链接 `libc++_static` / `libc++abi` / `libunwind`）；Rust 部分兼容（`RustCompat`） |
| 大 TU | 源文件 > 200 KB 或预处理后 > 800 KB 时自动跳过 `flattening` |

完整矩阵与限制说明见 [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md)。

## 仓库结构

```
repo_main/
├── ci/                 # 构建脚本、TOML 预设、nk_ad.c 运行时反调试模块
│   ├── build-lucifer.sh    # NDK 交叉编译入口
│   ├── build-nk.sh         # 插件本体构建
│   ├── nk_ad.c              # 运行时反调试（v11.1）
│   └── *.toml              # 预设配置
├── .github/workflows/  # CI：build-obf.yml / build-plugin.yml / build-apk.yml
├── android/            # 手机版 Android App（远程编译客户端）
├── projects/           # 示例工程与工具工程
├── proj/               # 默认源码目录（SRC_DIR）
└── docs/               # 本套文档
```

## 运行时反调试（`nk_ad`）

`ci/nk_ad.c` 不在混淆范围内，单独编译为 `nk_ad_extra.o` 链入。能力覆盖 TracerPid 交叉校验、watchdog 子进程 + `PTRACE_SEIZE` 自检、maps 差分、模块 prologue 编码扫描、代码段 FNV 哈希基线、直接 `svc #0` 系统调用、工具指纹（Frida / Xposed / LSPosed / Zygisk / Riru / eDBG / QBDI）、JNI/ART 函数表校验、硬件断点槽探测。命中即 `_exit(97)` 静默退出；`NK_AD_DEBUG=1` 时写 stderr 与 `/data/local/tmp/nk_ad.log`。

清单与取舍见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#运行时反调试模块-nk_ad)。

## 版本与许可

- 当前版本记录：[docs/CHANGELOG.md](docs/CHANGELOG.md)
- 许可：MIT，`Copyright (c) 2026 nk`，见 [LICENSE](LICENSE)
- 项目名称：**NK**（旧名已全面废弃，仅在 [docs/CREDITS.md](docs/CREDITS.md) 作为来源说明保留）
- 本仓库为私有仓库，未开源分发
