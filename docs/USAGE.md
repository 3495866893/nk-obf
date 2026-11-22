# 使用指南

从构建插件到拿到加固后的 Android `.so`，全流程四步：**构建插件 → 写 TOML → 挂 `-fpass-plugin` 编译 → 真机验证**。

---

## 1. 构建插件

需要 LLVM 21 源码树与对应构建产物。

```bash
# 假设 LLVM 21 已构建在 $LLVM_BUILD（含 bin/clang++、lib/cmake/llvm）
export LLVM_DIR=$LLVM_BUILD/lib/cmake/llvm
bash ci/build-nk.sh          # 输出 libNk.so
# 或直接用仓库自带 CI（.github/workflows/build-plugin.yml）产出制品
```

| 输入 | 含义 |
| --- | --- |
| `LLVM_DIR` | LLVM 21 的 CMake 配置目录 |
| 输出 | `libNk.so`（LLVM pass 插件） |
| 额外产物 | `nk_ad_extra.o`（仅当构建了 `ci/nk_ad.c`） |

## 2. 单文件编译

```bash
clang++ -fpass-plugin=/path/libNk.so -O2 -c a.cpp -o a.o
```

最小可运行链路：

```bash
NK=1 NK_CONFIG=/path/nk.toml \
  clang++ -fpass-plugin=/path/libNk.so -O2 -c a.cpp -o a.o
```

| 环境变量 | 作用 |
| --- | --- |
| `NK=1` | 打开插件总开关（未设置时插件加载但不改写） |
| `NK_CONFIG=<toml>` | 指定配置文件路径；缺省用内置 preset |
| `NK_PRESET=<name>` | 直接指定预设名（等价于 TOML 里写 `preset = "<name>"`） |
| `NK_AD=1` | 编译期决定是否链接 `nk_ad_extra.o` |
| `NK_AD_DEBUG=1` | 运行时把反调试命中详情写 stderr 与 `/data/local/tmp/nk_ad.log` |
| `NK_AD_WLOCK=1` | 运行时打开文本段写保护（`text_wlock`，默认关） |

## 3. Android NDK 交叉编译

```bash
export NK=1
export NK_CONFIG=$PWD/ci/tuned.toml
bash ci/build-lucifer.sh <NDK_DIR> <PLUGIN_BUILD_DIR> <输出名>
```

| 参数 | 示例 | 说明 |
| --- | --- | --- |
| `<NDK_DIR>` | `/opt/android-ndk-r29` | NDK r29 根目录 |
| `<PLUGIN_BUILD_DIR>` | `$PWD/llvm-build` | 含 `bin/clang++` 与 `libNk.so` |
| `<输出名>` | `lucifer` | 产物名前缀 |

脚本内部做的事：定位 NDK 的 `ld.lld` / sysroot / `aarch64` 运行库 → 收集 `-I` 头文件路径 → 用自带 clang 21 + `libNk.so` 编译 → 链接 `libc++_static` / `libc++abi` / `libunwind` → 视 `NK_AD` 追加 `nk_ad_extra.o` → `llvm-strip` 后输出。

| 脚本环境变量 | 默认 | 说明 |
| --- | --- | --- |
| `SRC_DIR` | `proj` | 源码目录 |
| `SRC_FILES` | `mian.cpp defense.c` | 参与编译的源文件列表 |
| `DEFENSE_LVL` | `3` | 防御等级，映射到 nk_ad 的检查强度 |
| `CLANG` | `$PWD/llvm-build/bin/clang++` | 自定义 clang 路径 |
| `BRAND_FILE` | `ci/brand.txt` | 产物标记文件 |

## 4. 配置文件（TOML）

结构固定两段：`[global]` 控制全局行为，`[passes.<name>]` 逐个开关。

### 4.1 全局段

| 键 | 取值 | 说明 |
| --- | --- | --- |
| `preset` | `off` / `light` / `mid` / `max` | 内置预设，先套用再被下方覆盖 |
| `seed` | 十六进制字符串 | 固定随机种子，便于复现同一份产物 |
| `verbose` | `bool` | 打印每个 pass 的处理计数 |
| `trace` | `bool` | 更细的 per-function 日志 |
| `demangle_names` | `bool` | 日志中还原 C++ 符号名 |

### 4.2 核心 8 项全开示例

```toml
# nk-core8.toml —— 核心 8 项全开，实验性项全关
[global]
preset = "mid"
seed = "0xDEADBEEF"
verbose = true
trace = false
demangle_names = true

[passes.string_encryption]
enabled = true
probability = 60
anti_dump = true                     # 必须 true
force_content = [".*"]
skip_content = ["^%[0-9]*[a-zA-Z]$"]

[passes.constant_encryption]
enabled = true
iterations = 2
share_count = 8
globalize = true
globalize_prob = 100
substitute_xor = true
substitute_xor_prob = 100
skip_value = ["^0x0$"]

[passes.substitution]
enabled = true
probability = 60
iterations = 2

[passes.bcf]
enabled = true
probability = 60
iterations = 2
complexity = 10
entropy_chain = true
junk_asm = true
junk_asm_min = 4
junk_asm_max = 10
nested = true
create_func = true
only_junk_asm = false

[passes.split_blocks]
enabled = true
splits = 10
stack_confusion = true

[passes.indirect_branch]
enabled = true
use_stack = true
enc_jump_target = true

[passes.flattening]
enabled = true

[passes.anti_debugging]
enabled = true
probability = 60
```

> 大 TU（源 > 200 KB 或预处理 > 800 KB）会自动跳过 `flattening`，无需在配置里额外处理。

### 4.3 关闭与调参

```toml
[passes.anti_hooking]        # 真机误报多，默认关
enabled = false

[passes.mba]                 # 实验性：体积换强度
enabled = true
probability = 40
layers = 2
heuristic = true
```

## 5. 反调试开关

`anti_debugging` 只在 IR 层插 `call __nk_ad_check()`，真正的逻辑在 `ci/nk_ad.c`。

| 阶段 | 开关 | 效果 |
| --- | --- | --- |
| 编译期 | `NK_AD=1` | 链接 `nk_ad_extra.o`，检查生效 |
| 编译期 | 不设 `NK_AD` | 调用点为空实现，零开销 |
| 运行期 | `NK_AD_DEBUG=1` | stderr + `/data/local/tmp/nk_ad.log` 输出命中项 |
| 运行期 | `NK_AD_WLOCK=1` | 打开 `.text` 写保护（默认关，部分 ROM 不兼容） |

命中即 `_exit(97)`，静默退出，不打印。调试时先开 `NK_AD_DEBUG=1` 定位是哪一项判定命中。

```bash
adb shell "NK_AD_DEBUG=1 ./your_binary"     # 或对 app 设置环境变量后启动
adb shell "cat /data/local/tmp/nk_ad.log"
```

## 6. GUI 工具

| 端 | 入口 | 依赖 | 功能 |
| --- | --- | --- | --- |
| 桌面（Windows） | `tools/NKBuild.py` | Python 3 + GitHub token | 选目录 → 选 pass → 一键云端编译 → 产物落 `Download/NKBuild` |
| 手机（Android） | `android/` App | 已授权的 token | 同上，移动端操作 |

两端都只是云端 CI 的客户端：真正的编译在 Actions runner 上完成，本地不构建 LLVM。

| 步骤 | 桌面 | 手机 |
| --- | --- | --- |
| 1 | 启动 `python tools/NKBuild.py` | 安装并打开 App |
| 2 | 填 GitHub token 与仓库 | 同左 |
| 3 | 选源码目录 / 文件 | 选源码目录 |
| 4 | 勾选 pass 与参数 | 同左 |
| 5 | 点「构建」，等日志 | 同左 |
| 6 | 产物落 `Download/NKBuild` | 同左 |

## 7. CI

`.github/workflows/build-obf.yml` 是主编译流程，`workflow_dispatch` 输入项：

| 输入 | 类型 | 说明 |
| --- | --- | --- |
| `src_dir` | string | 源码目录，如 `proj` |
| `src_files` | string | 空格分隔的源文件列表 |
| `cfg_only` | bool | 只产出配置、不编译 |
| `out_name` | string | 产物名前缀 |
| `anti_debug` | bool | 是否链接 `nk_ad_extra.o` |
| `skip_smoke` | bool | 跳过产物的冒烟校验 |
| `rebuild_clang` | bool | 强制重建 clang 21 工具链（慢，仅在 LLVM 变更时需要） |

另外三个工作流：`build-plugin.yml` 产出 `libNk.so`；`build-apk.yml` 打 Android App；`cleanup.yml` 清理过期制品与缓存。

调用示例（CLI）：

```bash
gh workflow run build-obf.yml \
  -f src_dir=proj \
  -f src_files="mian.cpp defense.c" \
  -f out_name=lucifer \
  -f anti_debug=true \
  -f skip_smoke=false \
  -f rebuild_clang=false
```

---

## 故障排查

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 插件加载即崩 / `undefined symbol` | clang 与 `libNk.so` 的 LLVM 版本不一致 | 用同一份 LLVM 21 构建插件与 clang |
| 编译通过但代码没变 | 未设 `NK=1`，或 `NK_CONFIG` 指向的文件没被读到 | `verbose = true` 看 pass 计数 |
| 后端 OOM / 生成 `brk` 后 SIGILL | 大 TU 走了 `flattening` | 拆源文件；确认阈值未改 |
| 程序启动即卡死 | `anti_dump = false` | 改回 `true` |
| 真机闪退，无日志 | `nk_ad` 判定命中，`_exit(97)` | `NK_AD_DEBUG=1` 复现看日志 |
| 启动即崩且用了 `function_call_obfuscate` | `dlopen` 不可用 | 关闭该项 |
| 体积暴涨 5–10× | 全开组合 | 按 `function_call_obfuscate` → `anti_debugging` → `function_wrapper` → `vector_obfuscation` 顺序回退 |
| 字符串仍是明文 | `skip_content` 命中，或 `probability` 抽样跳过 | 用 `force_content = [".*"]` 强制 |
