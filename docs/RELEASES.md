# 仓库结构 · 下载与发布

## 目录一览

```
nk-obf/
├── src/                    插件源码（LLVM 21 Pass，C++17，79 文件 / 2.5 MB）
│   ├── obfuscation/        16 个 pass 的实现
│   ├── include/            对外头文件
│   └── CMakeLists.txt      目标名 Nk → libNk.so
├── ci/                     构建脚本与混淆配置
│   ├── build-nk.sh         本地/CI 通用构建入口（NDK r29 + clang 21）
│   ├── nk_ad.c             运行时反调试看门狗（C，独立编译，不参与混淆）
│   ├── nk-nofold.toml      大 TU 专用配置（跳过 flattening 的 7 pass）
│   └── *.toml              各种预设与二分排查配置
├── android/                手机端 GUI（Kotlin + Compose，包名 com.nk.nkbuild）
├── tools/                  桌面端 GUI + 卡密模块（Python，customtkinter）
├── docs/                   文档（功能 / 用法 / 架构 / 兼容性 / 变更 / 署名）
├── projects/               示例与在用工程
│   ├── jni/                手机端默认示例工程（含完整 XL 验证模块）
│   ├── probe/ probe2/      反调试自检小程序
│   └── tool/               nk_spawn（ptrace 附加器，用于验证反调试）
├── kit.tar.gz              插件预编译包（根目录 nk/，CI 直接解压使用）
├── README.md               项目首页
└── LICENSE                 MIT
```

## 下载

| 产物 | 位置 | 说明 |
|---|---|---|
| 手机端 APK | Releases `v1.8.1` → `NKBuild-mobile-v1.8.1-kami.apk` | Android 8.0+，签名固定，可覆盖升级 |
| 桌面端 EXE | Releases `v1.8.1` → `NKBuild-1.9.0-win64.exe` | Windows x64 单文件，免安装 |
| 混淆后的示例二进制 | Releases `nk-builds` → `lucifer` | 手机端 / 桌面端每次构建自动覆盖 |
| 工具链包 | Releases `nk-builds` → `nk-toolchain.tar.gz` | clang 21 + lld + sysroot 打包 |
| LLVM 预编译 | Releases `llvm-build-21.1.8` → `llvm-build.tar.zst` | CI 缓存用，232 MB |

> Release 资产不占 Actions artifact 配额，长期保留；artifact 只留 3 天。

## 卡密系统

- **可以随便输入**：服务端当前只校验信封（SM4-CBC + HMAC-SM3 + 时间窗 + check 签名），不校验卡密真伪，任意字符串都能通过，纯占位。
- **内置检查更新**：读云端 `api=ini` 的 `version` 与本地版本比对（也保留 GitHub 产物比对兜底）。
- **想彻底删掉卡密**：
  - 桌面端：删 `tools/xlverify.py`，把 `tools/NKBuild.py` 里 `import xlverify`、`_build_license_bar()`、`lic_*` 方法与 `start()` 开头 `if not self.lic_ok(): ...` 去掉（或让 `lic_ok()` 直接 `return True`）。
  - 手机端：删 `android/app/src/main/java/com/nk/nkbuild/XlVerify.kt`，删 `MainActivity.kt` 的 `KamiGate` 与 `App()` 里 `if (!st.licOk) KamiGate(...)`，再去掉 `BuildViewModel` 的 `licAuto/licLogin/licUnbind`。

## 版本约定

- 手机端 `versionName` 固定在 `1.8.1`，只递增 `versionCode`（避免每次都要改文案）。
- 桌面端文件名带版本号：`NKBuild-<主版本>-win64.exe`。
- 混淆产物命名 `<out_name>.cfg`，由 CI 上传到 Release `nk-builds`。
