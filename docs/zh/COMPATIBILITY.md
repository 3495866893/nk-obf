# 兼容性与已知限制

本篇讲 NK 在什么环境下能跑、真机上验证过什么、以及哪些情况会出事。

## 环境要求

| 项目 | 要求 | 说明 |
|---|---|---|
| LLVM | 21.x | 插件以 `libNk.so` 形式被 `-fpass-plugin` 加载，ABI 与 LLVM 大版本绑定 |
| NDK | r29 | 提供 clang 21 / lld / sysroot；CI 从 dl.google.com 现下（约 1 分钟） |
| 目标 ABI | aarch64-linux-android | 反调试部分含 AArch64 内联汇编，其它架构未验证 |
| `--target` API | android21 及以上 | 构建脚本固定 `--target=aarch64-linux-android21` |
| Android 版本 | minSdk 26 | 手机端 GUI 与产物都按 26 起 |
| CI | GitHub Actions（私有仓库） | 免费额度 2000 分钟/月；单次构建 1–3 分钟 |
| 桌面端 | Windows x64 | 单文件 exe，无需装 Python |
| 构建机内存 | ≥ 8 GB（建议 16 GB） | 大 TU + 全 pass 时 clang 峰值很高，实测 16 GB 机器可用内存会被压到 119–398 MB |

## 真机验证记录

| 设备 | 系统 | 结论 |
|---|---|---|
| realme GT5 Pro（`RMX3888`） | Android 16 / SDK 36 | 混淆产物正常启动并打印自家界面；`NK_AD_DEBUG=1` 日志 `TracerPid=0 wd=0 hooked=0`（零误报）；被 `nk_spawn` 附加时 `exit 97` |
| realme（`RMX3300`） | Android 15 / SDK 35，KernelSU | 同上；root 域（`u:r:ksu:s0`）下第一次 `ptrace` 也返回 EPERM，因此「只认外来 tracer」的判定才成立 |
| 8 pass 产物 | — | 连续启动 **100 次零卡死** |
| 启动耗时 | — | 2–4 秒，主要是应用自身 `httpget_compat` 的 select 3 秒超时，不是混淆 |

## 已知限制

1. **大 TU 跳过 `flattening`**：源文件 > 200 KB 或预处理 > 800 KB 的编译单元走 `ci/nk-nofold.toml`（7 pass）。理由见 [CHANGELOG](CHANGELOG.md) 阶段 D：全 8 pass 编大 TU 会产生 `brk #0xf000` 哨兵，运行即 SIGILL，且后端内存会爆。
2. **`anti_dump=false` 的组合不可用**：实测会卡在全局构造器（`__dl_soinfo::call_constructors`），表现为单线程 100% CPU、无输出。默认配置 `anti_dump=true`。
3. **`anti_hooking` 默认关闭**：完整性基线在本环境始终不匹配（每次 CI 生成的二进制布局不同），开启必然误判。
4. **`anti_class_dump` / `objc` 不可选**：仅对 ObjC / Java 类转储场景有意义，Android 原生工程用不到。
5. **混淆不等于不可破解**：它把逆向成本推高（字符串不可读、控制流被扁平化、常量被分裂），但拿得到足够时间和算力的人仍然能还原。
6. **反调试依赖 `/proc` 与 `svc` 直连**：某些深度定制 ROM 若把 `/proc/self/maps` 内容改写，看门狗会 fail-open（宁可放过也不误杀），此时防护等级下降。
7. **私有仓库**：所有缓存、Release、产物都在这一个仓库下；换仓库需要同步改 `%APPDATA%\NKBuild\config.json` 与手机端设置页。

## 工具链版本锁定

| 组件 | 锁定版本 | 位置 |
|---|---|---|
| LLVM | 21.1.8 | workflow 的 `LLVM_VER`；预编译包 `llvm-build-21.1.8` |
| NDK | r29 | workflow 的 `NDK_DIR=/mnt/work/android-ndk-r29` |
| clang | 21（随 LLVM / NDK 提供） | `--target=aarch64-linux-android21` |
| 插件宿主编译器 | 系统 `clang++` 或 `g++` | `-fPIC -O2 -std=c++17 -fno-rtti`，LLVM 符号留 UND |
| CMake + Ninja | 任意近三年版本 | 只用来编 `libNk.so`，目标名 `Nk` |
| Kotlin | 2.0.21 | `android/build.gradle.kts` |
| Compose BOM | 2024.12.01 | `android/app/build.gradle.kts` |
| Haze | 1.5.3 | `dev.chrisbanes.haze:haze` + `haze-materials` |
| OkHttp | 4.12.0 | GitHub API 与卡密服务 |
| kotlinx.coroutines | 1.9.0 | 上传 / 轮询 / 下载 |
| JVM target | 17 | `kotlinOptions.jvmTarget` |
| compileSdk / targetSdk | 35 / 35 | 手机端 |

## 目标平台矩阵

| 项 | 值 | 说明 |
|---|---|---|
| ABI | `arm64-v8a`（aarch64） | 反调试里有 AArch64 内联汇编，其它架构没有等价实现 |
| `--target` | `aarch64-linux-android21` | 构建脚本写死 |
| 产物形态 | ELF，PIE 可执行 | 链接 `-Wl,--noinhibit-exec`，`--ld-path=ld.lld` |
| runner | Ubuntu x86_64 | 交叉编译到 aarch64 |
| 桌面端宿主 | Windows x64 | 只要一个 exe，不装 Python |
| 手机端宿主 | Android 8.0+（`minSdk 26`） | 低于 26 装不上 |

## 未验证或明确不支持的组合

1. **32 位 / x86 ABI**：`nk_ad.c` 里的 `svc` 探针、`perf_event_open` 断点槽都是 AArch64 形态。
2. **非 Android 目标**：插件本身是 IR 级改写，理论上能挂到其它 triple；但 `__nk_ad_check()` 依赖 `/proc`、`/data/local/tmp`，直接编 Windows/iOS 产物会在链接期缺符号。
3. **LLVM 20 或 22**：PassPlugin ABI 与大版本绑定，插件加载即失败（`undefined symbol: llvmGetPassPluginInfo` 或断言崩溃）。
4. **NDK r28 及更早**：clang 版本低于 21，不认当前 IR 形态，`-fpass-plugin` 加载后立刻报版本不符。
5. **自建 Actions runner**：workflow 里多处写死 `/mnt/work`、`/tmp/nk_o_*`，换路径要同步改。

## 常见报错速查

| 报错 | 含义 | 处理 |
|---|---|---|
| `exit code 143` + `runner has received a shutdown signal` | job 被 SIGTERM（内存耗尽） | 大 TU 已在自动降级；仍失败就再关掉 `bcf` |
| `Illegal instruction` | 产物里存在 `brk`/`udf` 哨兵 | 用 `docs/figs` 同款脚本扫 `PT_LOAD` + `PF_X` 段；把该 TU 加入大 TU 策略 |
| `Undefined temporary symbol .L_v…` | `string_encryption` 的 TLS 模型问题（历史 bug） | 已在插件侧修复；确认 `libNk.so` 是最新 kit |
| `duplicate symbol: cJSON_*` | 同一个 `.c` 既被提交又被 `#include` | 工具会自动剔除被 include 的源文件；手工构建时别把 `res/cJSON.c` 放进 `SRC_FILES` |
| `Artifact storage quota has been hit` | Actions artifact 配额满 | 清旧 artifact；二进制走 Release，artifact 只留 3 天 |
| `INSTALL_FAILED_UPDATE_INCOMPATIBLE` | 新包签名与旧包不同 | 已固定 `android/keystore/nkbuild.p12`；如仍报错先卸载旧版 |
