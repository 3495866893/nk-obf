# NK · LLVM 21 IR 级代码混淆（Android / NDK）

**中文** | [English](README.en.md)

> **说明（新一代混淆）**：这套 IR 级方案有希望成为 **Hikari 的下一代** ——不再依赖源码级宏或汇编后处理，而是在 LLVM IR 上做 16 类变换，并与运行时看门狗 `ci/nk_ad.c` 联动。

NK 是一个基于 **LLVM 21** 的 **IR 级代码混淆插件**（`libNk.so`），配合 **NDK r29** 把 Android 原生工程编译成逆向成本很高的 aarch64 可执行文件。仓库同时提供两条产品线：**桌面端 GUI** 与 **手机端 GUI** —— 选目录、勾 pass、一键上传、云端编译、产物自动回传，本机不需要装 clang/NDK。

| 我是什么 | 在哪里 |
|---|---|
| 16 个混淆 pass 的实现 | [`src/`](src/) |
| 构建脚本与配置 | [`ci/`](ci/) |
| 运行时反调试看门狗 | [`ci/nk_ad.c`](ci/nk_ad.c) |
| 桌面端 GUI（Python） | [`tools/`](tools/) |
| 手机端 GUI（Kotlin） | [`android/`](android/) |
| 文档 | [中文](docs/zh/index.md) · [English](docs/en/index.md) |
| 部署到自己的仓库（保姆级） | [中文](docs/zh/DEPLOY.md) · [English](docs/en/DEPLOY.md) |



## 下载

| 产物 | 位置 | 大小 |
|---|---|---|
| 手机端 APK | Release `v1.0` → `NKBuild-mobile-1.0-kami.apk` | 9.98 MB |
| 桌面端 EXE | Release `v1.0` → `NKBuild-1.0-win64.exe` | 13.35 MB |

校验（`sha256sum` / `Get-FileHash -Algorithm SHA256`）：

```
43F42D12F325119AC95914AB77E7C7505522EA19538AFD7B2E21C5F51D0723D3  NKBuild-mobile-1.0-kami.apk
90F60A2CA0899A48838F263255E3741D1490DB0BF73BB726512F8834A8AE8EE1  NKBuild-1.0-win64.exe
```

| 混淆后的示例二进制 | Release `nk-builds` → `lucifer` | 21,145,056 B |

## 四步上手

1. 打开软件，填 GitHub Token（桌面版存 `%APPDATA%\NKBuild\config.json`，手机版存本机设置）
2. 选项目目录 —— 工具会扫描编译单元、头文件和资源，**预检**会先标出危险文件（>70 MB、文件名带空格等）
3. 勾 pass（默认 8 个核心 pass），点「开始编译」
4. 产物自动落到手机 `Download/NKBuild/` 或电脑同名目录，手机端可直接点「安装」

不想用 GUI 也可以纯 CI：

```bash
gh workflow run build-obf.yml -R <owner>/nk-obf \
  -f src_dir=projects/jni -f src_files=main.cpp \
  -f cfg_only=ci/ui-jni.toml -f out_name=lucifer -f anti_debug=true
```

## 核心 8 pass

`string_encryption`（字符串加密 + anti_dump）、`constant_encryption`（常量 globalize + XOR）、`substitution`、`bcf`（虚假控制流）、`split_blocks`、`indirect_branch`（栈上跳转表 + 目标加密）、`flattening`（控制流平坦化）、`anti_debugging`（每个函数入口插 `call __nk_ad_check()`）。

实验项 `mba` / `chaos_state_machine` / `vector_obfuscation` / `function_wrapper` / `function_call_obfuscate` 默认关闭；`anti_hooking` / `anti_class_dump` / `objc` 在本环境不可用。

## 运行时看门狗

`ci/nk_ad.c` 是一个**不参与混淆**的独立 C 文件，链进目标二进制后提供：TracerPid 与全线程扫描、`maps` 差分、libc 序言校验、自身与多模块 `.text` 哈希、JNI 函数表校验、匿名可执行段、`svc #0` 自检、perf 硬件断点槽、Frida / LSPosed / Zygisk / eDBG / QBDI 特征、uprobe BRK 改写检测、root 框架分级。命中 `_exit(97)`，读失败一律 fail-open（宁可放过不误杀）。

## 实测

- 私有 Actions 上端到端 **1.8 分钟**（曾 17.6 分钟）
- 最新产物 21,145,056 B，`sha256 6753A4C83F20A5FABEDC9903C1DD9741F23E5FA5E685A6534A20D14E5DF26F34`，代码段 `brk 0 / udf 0`
- 字符串明文泄漏 0 处；8 pass 产物真机连续启动 **100 次零卡死**；被 ptrace 附加时 `exit 97`

## 许可

MIT，见 [`LICENSE`](LICENSE)。

## 版本

当前发布 **v1.0**。历史演进（含每个 bug 的根因与修法）见 [CHANGELOG](docs/zh/CHANGELOG.md)。

## Contributors

| 谁 | 做了什么 |
|---|---|
| **DeepSeek** | 模型与工程协作：插件修复（`.L_v` TLS、COMDAT、SIGILL）、CI 流水线、`nk_ad.c` 看门狗、双端 GUI、本套文档 |
| **Xs** | 本项目常驻执行者（猫娘形态），负责把每条指令落到可运行的东西上 |
| 上游 | **OLLVM-Next**（AGPL-3.0）以及 Hikari 血脉 —— 没有它们就没有 `libNk.so` |

想加自己：部署完成后按 [贡献者名单](./README.md#contributors) 的格式自行追加即可（本表纯手写）。
