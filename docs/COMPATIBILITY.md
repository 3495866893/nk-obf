# 兼容性

NK 是 LLVM Pass 插件，**与 LLVM 版本强绑定**：pass API 与 `PassBuilder` 回调签名在 21 上固定，插件 ABI 也随之固定。下表是已验证的组合。

---

## 编译器

| 项 | 要求 | 说明 |
| --- | --- | --- |
| LLVM / clang | **21**（必需） | `-fpass-plugin` 的插件接口、PassBuilder 扩展点按 21 编译 |
| 构建插件 | LLVM 21 头文件 + 静态库 | 需要 `LLVMConfig.cmake` 或 `llvm-config` |
| 插件产物 | `libNk.so` | 与宿主 clang 的 LLVM 版本必须一致 |
| 前端语言标准 | C99 / C11 / C17、C++11 / C++14 / C++17 / C++20 | IR 层改写，不依赖语言标准 |
| 优化级别 | `-O0` / `-O1` / `-O2` / `-Os` | 建议 `-O2`；`-O0` 下 `flattening` 收益下降 |

版本不匹配的典型症状：`undefined symbol` 于 `llvm::PassBuilder`、加载即崩、pass 静默不执行。

## 平台

| 平台 | Triple | 工具链 | 状态 |
| --- | --- | --- | --- |
| Android arm64 | `aarch64-linux-android21` | NDK r29 + clang 21 | 主目标 |
| Android armv7 | `armv7a-linux-androideabi21` | NDK r29 | 部分 pass 未适配（`vector_obfuscation` 无 NEON 128 位路径） |
| Windows x86_64 | `x86_64-pc-windows-msvc` | clang 21 + MSVC 运行库 | 独立分支维护 |
| Linux x86_64 | `x86_64-unknown-linux-gnu` | clang 21 | 加载方式与单文件用法相同 |
| macOS / iOS | — | — | 未验证 |

Android 侧构建脚本为 `ci/build-lucifer.sh`（NDK r29 + clang 21），目标 API 21。

## 语言与运行库

| 语言 | 支持度 | 备注 |
| --- | --- | --- |
| C | 完整 | 核心用例 |
| C++ | 完整 | 标准库需**静态链接**：`libc++_static` + `libc++abi` + `libunwind` |
| Rust | 部分 | 走 `RustCompat` 兼容 pass；`flattening` / `bcf` 在 Rust MIR 生成代码上效果不稳 |
| Objective-C | 有限 | `objc` 与 `anti_class_dump` 默认关闭 |
| Java / Kotlin | 不适用 | 仅 `anti_class_dump` 对类结构做处理 |

C++ 动态链接 `libc++_shared.so` 时，字符串加密与异常展开路径会出现符号解析问题，务必静态链接。

## 目标文件与链接

| 项 | 要求 |
| --- | --- |
| 段合并 | 默认关闭 `-ffunction-sections` 之外的激进分段；COMDAT 函数（`linkonce_odr`）不拆段 |
| LTO | 未验证；`-flto` 会让 pass 在 bitcode 层提前运行，与 16 项组合不保证 |
| 调试信息 | `-g` 可用，但会显著增大产物；发布构建去掉 |
| 异常 / RTTI | 可用；`flattening` 与 `invoke` 混用时已做 landing pad 保留 |

## 已知限制

| 限制 | 触发条件 | 表现 | 处理 |
| --- | --- | --- | --- |
| 大 TU 跳过 `flattening` | 源文件 > 200 KB 或预处理后 > 800 KB | 该 TU 不做平坦化（其余 pass 照常） | 拆分源文件；阈值不要改 |
| `anti_dump = false` | 关闭 `string_encryption` 的抗 dump | 程序卡死 | 保持 `anti_dump = true` |
| `anti_hooking` 误报 | 真机 ART 自修改、厂商 ROM | 命中即 `_exit(97)`，表现为闪退 | 默认关闭；开启前做机型回归 |
| `anti_class_dump` / `objc` | 纯 C/C++ 工程 | 无收益 | 保持关闭 |
| `mba` 体积 | `layers = 3` | 体积 +20–60% | 降到 1–2 层 |
| `function_call_obfuscate` | Android 无 `dlopen` 环境 | 启动即崩 | 关闭该项 |
| `chaos_state_machine` | 块数 > `max_blocks`（10000） | 跳到经典 `flattening` | 调低 `warmup` 或接受回落 |
| `vector_obfuscation` | `width != 128` | 配置被忽略（arm64 只有 NEON） | 固定 128 |
| 反调试日志 | `NK_AD_DEBUG=1` | 写 `/data/local/tmp/nk_ad.log` | 发布构建去掉该变量 |

## 混淆强度与预期

| 目标 | 效果 |
| --- | --- |
| 抗 `strings` / 静态字符串扫描 | 高（`string_encryption` + `anti_dump`） |
| 抗 IDA / Ghidra 反编译可读性 | 高（`flattening` + `bcf` + `substitution`） |
| 抗 Frida / Xposed 动态注入 | 中高（`nk_ad` 指纹 + 反 hook，取决于版本） |
| 抗内存 dump | 中（字符串用后清零、常量池分散） |
| 抗专业逆向团队定向分析 | 低—中，只提高时间成本 |

混淆强度 ≠ 绝对安全。上述手段做的是抬高静态分析与动态调试的成本，不改变算法本身可被还原的事实。

## Pass × 平台 兼容矩阵

`◯` 完整可用，`△` 可用但有前提，`—` 不适用或未适配。

| Pass | Android arm64 | Android armv7 | Windows x86_64 | Linux x86_64 |
| --- | --- | --- | --- | --- |
| `string_encryption` | ◯ | ◯ | ◯ | ◯ |
| `constant_encryption` | ◯ | ◯ | ◯ | ◯ |
| `substitution` | ◯ | ◯ | ◯ | ◯ |
| `bcf` | ◯ | ◯ | ◯ | ◯ |
| `split_blocks` | ◯ | ◯ | ◯ | ◯ |
| `indirect_branch` | ◯ | △ | ◯ | ◯ |
| `flattening` | ◯（大 TU 跳过） | ◯（大 TU 跳过） | ◯ | ◯ |
| `anti_debugging` | ◯（需 `NK_AD=1`） | △ | △（走 `inline_win`） | △ |
| `anti_hooking` | △（默认关） | △ | △（默认关） | △ |
| `mba` | △ | △ | △ | △ |
| `chaos_state_machine` | △ | △ | △ | △ |
| `vector_obfuscation` | ◯（`width = 128`） | — | — | △ |
| `function_wrapper` | △ | △ | △ | △ |
| `function_call_obfuscate` | △（依赖 `dlopen`） | △ | ◯ | ◯ |
| `anti_class_dump` | — | — | — | — |
| `objc` | △（ObjC 工程） | △ | — | △ |

`nk_ad`（`ci/nk_ad.c`）的检查项按 **aarch64 Android** 编写：`svc #0` 直调、`/proc/self/task/*/TracerPid`、`uname` 校验 `machine == aarch64` 都是该平台专属。桌面 x86_64 构建不设 `NK_AD`，反调试由 `anti_hooking` 的 `inline_win` / `inline_x86` 路径承担。

## 产物预算

| 指标 | 核心 8 项 | 全开 | 备注 |
| --- | --- | --- | --- |
| `libNk.so` | 单文件，约数十 MB 级 | 同左 | 与 LLVM 21 静态库链接 |
| 被加固 `.so` 体积 | ×2–3 | ×5–10 | 主要来自 `flattening` + `bcf` |
| 单 TU 编译时间 | ×1.5–2 | ×2–4 | `-O2`，arm64 |
| 编译峰值内存 | ×2–3 | ×3–5 | 大 TU 是主要风险点 |
| 运行期启动 | +5–15% | +20–60% | 首次解密与基线采样 |
| 稳态性能 | -10–40% | -30–60% | `flattening` 主导 |

## 构建系统集成

| 场景 | 接入点 | 注意 |
| --- | --- | --- |
| 手写命令行 | `-fpass-plugin=/path/libNk.so` | 每个参与编译的 TU 都要带 |
| CMake | `target_compile_options(... -fpass-plugin=...)` | 只作用于编译期，链接期不需要 |
| Gradle `externalNativeBuild` | `cmake { arguments "-DCMAKE_CXX_FLAGS=..." }` | 环境变量在 Gradle 进程里导出，子进程继承 |
| 仓库脚本 | `ci/build-lucifer.sh` | 传入 NDK 目录、插件目录、产物名 |
| 云端 CI | `build-obf.yml` | 输入项见 [USAGE.md](USAGE.md#7-ci) |

## 环境自检

```bash
clang++ --version                      # 期望 21.x
$LLVM_BUILD/bin/llvm-config --version  # 必须与 clang 同版本
ls $LLVM_BUILD/lib/libNk.so            # 插件是否已产出
readelf -d your.so | head              # 检查是否误链 libc++_shared
```

四条都对不上就别往下走：版本不一致时的报错往往出现在链接期，而不是加载插件的那一刻。

## 错误码与典型报错

| 表现 | 含义 |
| --- | --- |
| 退出码 `97` | `nk_ad` 判定命中，静默退出 |
| `SIGILL`（132） | 异常路径的 `brk` 哨兵被执行，通常是 `flattening` 在大 TU 上的残留 |
| `SIGSEGV` 于首次解密 | 字符串解密的密文段被优化掉或 `-O0` 下寄存器分配冲突 |
| `undefined symbol` 含 `PassBuilder` | 插件与宿主 clang 的 LLVM 版本不一致 |
| `relocation refers to a discarded section` | COMDAT 段被丢弃，见 CREDITS 的修复项 |
| 进程被 `OOM killer` 杀（137） | 大 TU 走了 `flattening` |
| `dlopen failed` | 用了 `function_call_obfuscate` 而运行环境无 `dlopen` |

## 验证矩阵

| 组合 | 结果 |
| --- | --- |
| clang 21 + NDK r29 + 核心 8 项 | 通过（arm64，API 21） |
| clang 21 + NDK r29 + 全开 | 通过但需逐项回退定位闪退 |
| clang 21 + Windows x86_64 + 核心 8 项 | 通过 |
| clang 21 + Linux x86_64 + 核心 8 项 | 通过 |
| clang 20 / 22 | 不通过（pass API 不匹配） |
| `-flto` | 未验证 |
| Rust `RustCompat` + `flattening` | 部分通过 |
