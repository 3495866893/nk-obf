# NK / nk-obf

> 本篇讲：NK 是什么、三条产品线怎么分工、仓库里每个目录放什么、四步跑通第一次构建，以及其余七篇文档该按什么顺序读。

NK 是一个基于 **LLVM 21** 的 **IR 级代码混淆插件**（产物 `libNk.so`，CMake 目标名 `Nk`），配合 **NDK r29** 把 Android 原生工程编译成「反逆向成本很高」的 aarch64 可执行文件。仓库 `3495866893/nk-obf` 为私有仓库，编译只在私有仓库的 GitHub Actions 上跑，本地不需要装 clang 或 NDK。

项目在发布版 v1.0 之前有过一版旧代号和旧仓库名，2026-11-23 全量改名为 `NK` / `nk-obf`：目录 `nk/`、产物 `libNk.so`、脚本 `ci/build-nk.sh`、环境变量 `NK` / `NK_CONFIG`，共 95 个文件。旧名与新名的逐项对照见 [CHANGELOG.md](CHANGELOG.md) 阶段 G。

## 三条产品线

三条线共用同一套 CI，区别只在「谁来点那个按钮」。

| 产品线 | 入口 | 技术栈 | 谁适合用 |
| --- | --- | --- | --- |
| 编译器插件 | `libNk.so` + `ci/build-nk.sh` | C++17 / LLVM PassPlugin | 手上有 LLVM 21 构建树、想自己接 CMake 或命令行 |
| 桌面端 GUI | `tools/NKBuild.py` | Python 3 + CustomTkinter，打包成 `NKBuild-1.0-win64.exe`（13.35 MB） | Windows 上选目录、勾 pass、点一下就等产物 |
| 手机端 GUI | `android/`（`com.nk.nkbuild`） | Kotlin + Jetpack Compose，`NKBuild-mobile-1.0-kami.apk`（9.98 MB） | 只有手机，源码也在手机上 |

两端 GUI 都只是云端 CI 的客户端：真正的编译发生在 Actions runner 上。用户侧的动作永远是「选目录 → 勾 pass → 上传 → 触发 → 下载」。

## 目录结构

| 路径 | 内容 | 备注 |
| --- | --- | --- |
| `src/` | 插件源码，79 文件 / 2.5 MB | 16 个 pass 各一个 `.cpp`，加 `Utils.cpp` / `CryptoUtils.cpp` |
| `ci/nk_ad.c` | 运行时看门狗壳（8 条 `#include "nk_ad_parts/NN_*.inc"`，分块合计约 2,100 行） | 单独编译、只过 `string_encryption`，入口符号每次构建随机 |
| `ci/build-nk.sh` | 构建 `libNk.so` | 用宿主 clang++/g++，只借自建 LLVM 的头文件 |
| `ci/build-lucifer.sh` | 交叉编译入口 | 逐文件 `-c`，大 TU 三级降级，链接 `nk_ad.o` |
| `ci/*.toml` | 30 余份配置 | `tuned.toml` 是默认，`nk-nofold.toml` / `ui-jni-light.toml` 是大 TU 兜底 |
| `.github/workflows/build-obf.yml` | 主编译流程 | `workflow_dispatch` + `push`（`proj/**`） |
| `.github/workflows/build-apk.yml` | 打手机端 APK | push `android/**` 自动触发 |
| `.github/workflows/build-plugin.yml` | 单独产出 `libNk.so` | 插件改动后手动跑 |
| `.github/workflows/cleanup.yml` | 清过期 artifact / 缓存 | 配额治理用 |
| `tools/NKBuild.py` | 桌面端 GUI ~1000 行 | 四张卡片 + 日志框 + 进度条 + ETA |
| `tools/xlverify.py` | 卡密校验（纯 Python） | SM4-CBC + HMAC-SM3 信封 |
| `android/` | 手机端工程 | `keystore/nkbuild.p12` 固定签名 |
| `proj/` | 冒烟用示例工程 | `mian.cpp` + `defense.c`，默认输入 |
| `docs/` | 文档 | 本目录是中文版，`docs/en/` 是对应英文版 |
| `kit.tar.gz` | 插件源码包（CI 用） | 按 `sha256(kit.tar.gz)[:12]` 做缓存键 |

仓库清理后的规模是 **294 文件 / 24.9 MiB**（清理前 410 文件 / 63.3 MiB）。

## 快速上手（四步）

### 第一步：拿 token

在 GitHub 上建一个 fine-grained token，只授权 `3495866893/nk-obf` 这一个仓库，权限两项：

| 权限 | 级别 | 用途 |
| --- | --- | --- |
| Contents | Read and write | 传源码 blob、建 tree、提交 commit、传配置 toml |
| Actions | Read | 读 run 状态、拉日志、下载 artifact |

token 不落仓库、不进 git，只存在本机（桌面端配置文件 / 手机端 `SharedPreferences`）。

### 第二步：选源码目录

桌面端点「源码」卡片选目录；手机端点「选择目录」用 SAF 挑文件夹。扫描器会分成两类：

- **可编译单元**：`.c .cc .cpp .cxx .c++ .s .asm`
- **待上传文件**：头文件、资源、`#include` 进来的 `.c/.cpp`

第二类必须一起传，否则 CI 上直接 `fatal error: 'imgui.h' file not found`。被 `#include` 的 `.c/.cpp` 会在扫描后被剔除出编译列表，避免 duplicate symbol。

### 第三步：勾 pass 与反调试

默认勾好核心 8 项（`string_encryption` / `constant_encryption` / `substitution` / `bcf` / `split_blocks` / `indirect_branch` / `flattening` / `anti_debugging`）。逐项说明见 [FEATURES.md](FEATURES.md)。

顶部的「反调试」总开关决定 `NK_AD=1` 还是 `NK_AD=0`；关掉时 `anti_debugging` pass 仍然开着，只是 `__nk_ad_check()` 编成空实现——直接关 pass 会让产物在静态初始化阶段 100% CPU 死循环。

### 第四步：上传、触发、取产物

点「开始」后：blob 上传（1/2/5/20 MB 都返回 201，上限 100 MB）→ 建 tree → 建 commit → `workflow_dispatch` → 轮询 run → 下载产物到 `Download/NKBuild`（手机端）或按顺序回退出的输出目录（桌面端）。

缓存命中时端到端 **1.8 分钟**；冷启动要重建 LLVM，约 100 分钟，默认被 `guard` 步骤拦住不允许进入。

```bash
# 纯命令行等价操作
gh workflow run build-obf.yml --repo 3495866893/nk-obf \
  -f src_dir=proj \
  -f src_files="mian.cpp defense.c" \
  -f config_file=ci/tuned.toml \
  -f anti_debug=true \
  -f out_name=lucifer
```

## 最新一次构建（可对照）

| 项 | 值 |
| --- | --- |
| run id | `45041678195`（success） |
| `build lucifer` 耗时 | 41 秒 |
| 产物大小 | 21,145,056 B |
| sha256 | `6753A4C83F20A5FABEDC9903C1DD9741F23E5FA5E685A6534A20D14E5DF26F34` |
| 代码段 | 16,086,400 B |
| 非法指令 | `brk 0 / udf 0` |
| 明文残留 | `yz.dujiuyue.cn` 0 处、`kami` 0 处 |

## 文档索引

| 文档 | 讲什么 | 什么时候读 |
| --- | --- | --- |
| [FEATURES.md](FEATURES.md) | 16 个 pass 的 key / 作用 / 默认值，`nk_ad.c` 的全量检测清单，上传预检规则 | 决定勾哪几项、判断哪个检查项把程序杀了 |
| [USAGE.md](USAGE.md) | 桌面端、手机端、纯 CI 三种用法，每个控件的作用，token 与产物落地位置，报错怎么读 | 第一次跑、或者跑失败了 |
| [ARCHITECTURE.md](ARCHITECTURE.md) | 五段流水线、pass 执行顺序、大 TU 三级降级、`nk_ad.c` 为何不混淆、CI 缓存与断点续接 | 想改流水线、或者要解释某个设计为什么长这样 |
| [COMPATIBILITY.md](COMPATIBILITY.md) | 环境要求、真机验证记录、已知限制、错误码 | 换机器、换 NDK、怀疑环境不对 |
| [CHANGELOG.md](CHANGELOG.md) | 阶段 A~G 的完整版本史，每条都是「问题 → 根因 → 修法 → 验证数据」 | 想知道某个坑是不是已经踩过 |
| [RELEASES.md](RELEASES.md) | 下载入口、卡密系统怎么用与怎么彻底删掉、版本号约定 | 要发布、要分发给别人 |
| [CREDITS.md](CREDITS.md) | 依赖与致谢 | 合规核对、二次分发 |

## 术语

| 术语 | 含义 |
| --- | --- |
| pass | LLVM 的一个 IR 改写单元；NK 有 16 个，按固定顺序跑 |
| TU | translation unit，一个 `.c/.cpp` 编译单元 |
| 大 TU | 源 > 200 KB 或预处理后 > 800 KB 的编译单元，会触发降级 |
| 核心 8 项 | 默认开启的 8 个 pass |
| `nk_ad` | `ci/nk_ad.c` 编出来的运行时看门狗，负责反调试/反注入/反模拟器 |
| 软分 | 只累加计分、不当场退出的证据；累计到 3 分才 `_exit(97)` |
| 断点续接 | 手机端被系统杀掉后，靠持久化的 `run_id` 接着轮询同一个 run |

## 边界

NK 抬高的是静态分析与动态调试的时间成本，不改变「算法本身可被还原」这个事实。核心 8 项把代码体积推到 2–3 倍、稳态性能压掉 10–40%，换来的是 IDA 里几乎不可读的控制流；它不会让一个明文密钥变成安全密钥，也不会让一个逻辑漏洞消失。
