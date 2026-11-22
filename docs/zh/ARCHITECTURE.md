# 架构：从源码目录到加固产物

> 本篇讲：五段流水线怎么串起来、16 个 pass 在 IR 上的固定执行顺序、大 TU 的三级降级为什么长这样、`nk_ad.c` 为什么不参与混淆、CI 缓存与 Release 怎么分工、断点续接与任务历史用什么数据结构。

## 分层视图

```
源码目录 (.c/.cpp + 头文件 + 资源)
   │  GUI 扫描：编译单元 / 待上传文件
   ▼
GitHub API：blob → tree → commit（+ ci/ui-<proj>.toml）
   │  workflow_dispatch
   ▼
Actions runner
   │  ┌────────────── 缓存层 ──────────────┐
   │  │ LLVM 21 构建树（Release 资产 232 MB）│
   │  │ libNk.so（artifact，按 kit 指纹）    │
   │  │ NDK r29（artifact）                 │
   │  └────────────────────────────────────┘
   ▼
clang 21 + NDK r29 + libNk.so
   │  逐文件 -c：IR 改写 → 后端 → .o
   ▼
链接：.o + nk_ad_extra.o（不混淆的 C）→ lucifer
   │  objcopy --update-section .comment
   ▼
Release `nk-builds` / artifact → 客户端下载 → Download/NKBuild
```

插件只负责让二进制难读，`nk_ad` 只负责让运行环境不对，两者通过一个外部函数符号 `__nk_ad_check()` 连接。

## Pass 在 IR 上的执行顺序

顺序写死在插件里，配置只能决定「某项跑不跑」，不能调整先后。

| 序 | pass | 阶段 | 为什么在这个位置 |
| --- | --- | --- | --- |
| 1 | `string_encryption` | 数据 | 最早的字符串形态最完整，便于全量收集 |
| 2 | `constant_encryption` | 数据 | 常量池先建好，后续 pass 才能复用 |
| 3 | `substitution` | 指令 | 单指令级改写，不改变块结构 |
| 4 | `mba` | 指令 | 同类改写，放在 `bcf` 前便于观察体积增量 |
| 5 | `vector_obfuscation` | 指令 | 必须赶在控制流改写前保持向量形态 |
| 6 | `bcf` | 控制流 | 先建假分支 |
| 7 | `split_blocks` | 控制流 | 再切块，切完的块才进入平坦化 |
| 8 | `chaos_state_machine` | 控制流 | 优先尝试，块数超限自行回落 |
| 9 | `flattening` | 控制流 | 兜底平坦化，也是大 TU 的跳过点 |
| 10 | `indirect_branch` | 控制流 | 平坦化后再把残余跳转改间接 |
| 11 | `function_wrapper` | 结构 | 函数边界已定，做代理层 |
| 12 | `function_call_obfuscate` | 结构 | 调用点改写，最后做 |
| 13 | `anti_debugging` | 注入 | 每函数入口插 `call __nk_ad_check()` |
| 14 | `anti_hooking` | 注入 | 完整性基线，默认关 |
| 15 | `anti_class_dump` | 注入 | 仅 ObjC/Java |
| 16 | `objc` | 前端 | 仅 ObjC 工程 |

三条硬理由：

1. **加密必须在控制流改写之前**。否则解密调用本身会被平坦化，每次取字符串都要过一遍状态机，运行期开销直接爆炸。
2. **控制流从轻到重**。`bcf` 加的假分支会被 `split_blocks` 切、被 `flattening` 压进状态机；反过来做，`bcf` 就要在已平坦化的 IR 上找谓词，效果差且慢。
3. **反调试注入放最后**。保证每个真实函数入口（包括前面 pass 新生成的函数）都有一个检查点。

## 插件加载与配置

| 阶段 | 入口 | 行为 |
| --- | --- | --- |
| 加载 | `llvmGetPassPluginInfo` | 注册 PassBuilder 回调；`ci/build-nk.sh` 用 `nm -D` 校验这个符号存在 |
| 总开关 | `NK=1` | 未设置则加载但不改写 |
| 配置 | `NK_CONFIG` → TOML；`NK_PRESET` → 预设 | TOML 优先，其次预设，最后编译期默认 |
| 执行 | `runOnModule` | 逐 pass 判定 `enabled` |
| 收尾 | `verifyModule` | IR 验证失败则丢弃改动并报错 |

`ci/build-nk.sh` 的几个关键点：

```bash
# 插件用系统编译器，只借自建 LLVM 的头文件
CXX=clang++        # 或 g++
FLAGS=(-fPIC -O2 -std=c++17 -fno-rtti -fexceptions -DLLVM_NEXT=1 \
       -DNK_TARGET_LLVM_VERSION=21 -w "-DGIT_COMMIT_HASH=\"$HASH\"")
"$CXX" "${FLAGS[@]}" -shared -Wl,-soname,libNk.so ... -o "$BUILD/libNk.so"
nm -D "$BUILD/libNk.so" | grep -q llvmGetPassPluginInfo
```

为什么不用自建 clang 编插件：自建 clang 的默认 triple 是 `unknown`，编宿主插件直接报 `unknown target triple 'unknown'`。所以插件走系统编译器，LLVM 符号留 UND，运行时由宿主 clang 提供——这也是「插件与宿主 LLVM 版本必须一致」的根源。

## 大 TU 的三级降级

这是整套流水线里最容易炸的一段，事故过程见 [CHANGELOG.md](CHANGELOG.md) 阶段 D。现状是逐文件编译 + 三级降级。

### 判定：看预处理字节数，不看源文件大小

```bash
fsz=$(stat -c%s "$f")
ppsz=$(timeout 300 "$CLANG" "${CF[@]}" -E -P "$f" 2>/dev/null | wc -c)
```

必须这么判的原因：`main.cpp` 只有 1.3 KB，但 `#include` 了 `pz.h` / `xz.h` / `Encrypt.h`，预处理后 3.0 MB；按源大小判会直接漏判，8 个 pass 全开把 runner 内存吃光。反过来 `imgui.cpp` 960 KB，预处理后也是几 MB——两种都要靠 `-E -P | wc -c` 才看得见。

### 三级策略

| 级别 | 触发条件 | 配置 | 优化级别 | 实际跑的 pass |
| --- | --- | --- | --- | --- |
| 正常 | 源 ≤ 200 KB 且 预处理 ≤ 800 KB | 用户选的 TOML | `-O2` | 勾了几个跑几个 |
| 二级：`nofold` | 源 > 200 KB 或 预处理 > 800 KB | `ci/nk-nofold.toml` | 源 > 300 KB 或 预处理 > 1.5 MB 时 `-O1` | 7 个：去掉 `flattening`，其余照常 |
| 三级：`light` | 同上，但 `nofold` 不存在 | `ci/ui-jni-light.toml`（复制成 `presets/nk-light.toml`） | 同上 | 3 个：`string_encryption` + `substitution` + `anti_debugging` |

`nofold` 保留的 7 项是 `string_encryption` / `constant_encryption` / `substitution` / `bcf` / `split_blocks` / `indirect_branch` / `anti_debugging`，`flattening` 显式 `enabled = false`。`light` 把 `constant_encryption` / `bcf` / `split_blocks` / `indirect_branch` 也关掉。

### 逐文件 `-c` 的两个好处

1. **可定位**：17 个 TU 一次丢给 clang，一次 SIGTERM 就全线失忆；逐文件能打印「停在第几个、耗时多少、可用内存多少」。
2. **可单独降级**：降级是 per-file 的，`imgui.cpp` 走 `light`，同工程的小文件仍走全量配置。

配套细节：

| 细节 | 做法 |
| --- | --- |
| 中间产物 | 每 4 个文件清一次 `/tmp/nk_o_*`，防 15 MB 级头文件把磁盘吃光 |
| 单文件超时 | `timeout 1800` |
| 链接 | `--ld-path=$TC/bin/ld.lld`，`-Wl,--noinhibit-exec` |
| 失败诊断 | 自动 `-S -emit-llvm` 转储 IR，统计 `_v` 定义/声明/引用数，再生成 `.s` 核对 `.L_v` 标签 |

### 双保险：关掉 oomd + 16 GB swap

runner 上 `disk & deps` 步骤会停掉 `systemd-oomd` 并挂 16 GB swap，同时 `overcommit_memory=1`。实测 `imgui.cpp` 在 `flattening`/`indirect` 下峰值能到 10 GB+，而 runner 可用内存会掉到 **119–398 MB**。这两项只是保险，真正解决问题的是降级策略。

## `nk_ad.c` 为什么不参与混淆

`ci/nk_ad.c` 单独编译成 `nk_ad_extra.o`，**不带 `-fpass-plugin`**，再进链接列表。四条理由：

1. **自指问题**。一旦被混淆，「检查自己有没有被改」这件事本身就要过状态机；`flattening` 会改变代码段大小与布局，哈希基线自己就对不上。
2. **实测会炸**。把 `nk_ad.c` 丢进 `SRCS` 参与混淆，直接产生 `Undefined temporary symbol .L_vXXXX` 汇编错误。
3. **可调试**。反调试是唯一需要在真机上反复看日志的部分，保持原样才能读栈、对源码行。
4. **可整体关闭**。`NK_AD=0` 时用 `-DNK_AD_OFF` 编成空实现，`__nk_ad_check()` 与 `nk_die_r()` 两处提前返回，文件其余部分保留（这样 `anti_debugging` 注入的调用点仍然有定义）。

```bash
# 反调试模块：单独编译，不经混淆
"$CLANG" -x c --target=aarch64-linux-android21 --sysroot="$SYS" \
  "-resource-dir=$TC/lib/clang/21" -O2 -fPIE -fno-ident $NK_AD_DEF \
  -c "$PWD/ci/nk_ad.c" -o "$PWD/nk_ad_extra.o"
```

**为什么不能干脆关掉 `anti_debugging` pass**：关掉之后注入点没了，但实测产物在静态初始化阶段 100% CPU 死循环、输出 0 字节。所以 pass 永远开着，「关闭反调试」只换实现。

## CI 缓存与 Release 分发

### 三层缓存

| 层 | 键 | 存放 | 命中时省下 |
| --- | --- | --- | --- |
| LLVM 21 构建树 | 固定 Release `llvm-build-21.1.8`（`llvm-build.tar.zst`，232 MB） | Release 资产 | 约 100 分钟全量编译 |
| `libNk.so` | `sha256(kit.tar.gz)[:12]` → artifact `nk-plugin-<指纹>` | artifact（3 天） | 插件编译，约 2 分钟 |
| NDK r29 | artifact `ndk-r29-cache` | artifact（3 天） | 下载 786 MB，约 1 分钟 |

恢复顺序都是「Release → artifact API 最新未过期 → 现下 / 现编」。插件缓存带 3 次重试，解压后校验 `libNk.so` 存在才算命中。

### 为什么产物走 Release

私有仓库的 Actions 存储只有 500 MB。历史上一度堆到 **289 个 artifact / 24.4 GiB**，直接把配额撑爆：

```
Failed to CreateArtifact: Artifact storage quota has been hit
```

治理动作：

| 动作 | 效果 |
| --- | --- |
| `llvm-build` / `ndk-r29-cache` 不再上传 artifact | 单份 232 MB / 786 MB，不再重复堆 |
| 产物改发 Release `nk-builds` | Release 资产不占 Actions 存储配额 |
| artifact `retention-days: 3` | 三天自动过期 |
| 清理到 5 个 / 16.8 MiB | 配额恢复可用 |

### 端到端时间

| 改动 | 效果 |
| --- | --- |
| `disk & deps` 由固定 apt 改为按需安装 | 7.5 分钟 → 0.02 分钟 |
| `smoke test` 默认 `skip_smoke=true` | 4 分钟 → 0 |
| LLVM / 插件 / NDK 三层缓存 | 冷启动 100+ 分钟 → 命中即跳过 |
| 合计 | **17.6 分钟 → 1.8 分钟** |

最新一次全量命中：run `45041678195`，`build lucifer` **41 秒**，产物 21,145,056 B，sha256 `6753A4C8…`。

## 断点续接与任务历史

手机端的「切后台不丢任务」由两样东西撑起来：前台服务保活 + 持久化的 run 坐标。

### 状态机

| 阶段 | `run_id` | 写入者 |
| --- | --- | --- |
| 开始上传 | `0`（`markJob` 落 `proj/out/eta/start`） | 上传前 |
| 触发成功 | 真实 run id（`addJob` 记一条历史） | dispatch 返回后 |
| 下载完成 | 不变（`markJobDone` 把历史那条标 `done=true` + `file`） | 下载后 |
| 手动清理 | —（`clearJob` 移除五个键） | 用户点取消 |

应用重启时：`run_id == 0` → 从头重来；`run_id != 0` → 接着轮询同一个 run，日志与产物都续上。被杀期间 run 仍在 runner 上跑，所以「续接」几乎总能拿到产物。

### 数据结构

`SharedPreferences("nkbuild")` 里的两条记录：

```kotlin
// 当前任务（单条）
run_id: Long        // 0 = 还没触发
job_proj: String    // 工程名
job_out: String     // 产物名
job_eta: Long       // 预计毫秒
job_start: Long     // 派发时间戳

// 任务历史（JSON 数组，最多 30 条，同 run 去重、最新在前）
data class JobRecord(
    val runId: Long,
    val proj: String,
    val out: String,
    val startMs: Long,
    val file: String = "",
    val done: Boolean = false,
)
```

`saveJobs()` 用 `take(30)` 截断；`addJob()` 先过滤掉同 `runId` 再插到最前。历史卡片显示「工程·产物 + run id + 时间 + 是否已下载」，点一下就是用该 run id 重新下载。

## 产物链路

| 产物 | 来源 | 去向 |
| --- | --- | --- |
| `libNk.so` | `ci/build-nk.sh` | artifact 缓存；`build-plugin.yml` 单独发布 |
| `nk_ad_extra.o` | `ci/nk_ad.c` 单独编译 | 直接进链接列表 |
| `lucifer` | `ci/build-lucifer.sh` | Release `nk-builds` + artifact `lucifer-obf` |
| `nk-toolchain.tar.gz` | workflow 里现场拼（`source.properties` + banner + brand） | Release `nk-builds` + artifact |
| `NKBuild-1.0-win64.exe` | PyInstaller onefile | Release v1.0 |
| `NKBuild-mobile-1.0-kami.apk` | `build-apk.yml`（`assembleDebug` + 固定签名） | Release v1.0 |

品牌写入用 `objcopy`：

```bash
"$OBJCOPY" --update-section ".comment=$BRAND" "$f" \
  || "$OBJCOPY" --add-section ".comment=$BRAND" --set-section-flags ".comment=merge,strings,readonly" "$f"
```

`.comment` 段里是 `NK-Ollvm[QQ:...][TG:...][Bilibili:...]`，CI 的 `verify` 步骤会数 `NK-Ollvm` 出现次数、确认旧指纹 `TUSI` / `clang version` 不残留。

## 边界

| 不做的事 | 原因 |
| --- | --- |
| 替换用户的加密算法 | 只搬运与包裹，算法仍是用户的 |
| Java/Kotlin 层混淆 | 只处理原生代码，`anti_class_dump` 仅触及类结构 |
| 保证不可破解 | 抬高成本，不改变可还原性 |
| 在本地编 LLVM | 桌面/手机端都不装 clang，全部在 runner 上做 |
