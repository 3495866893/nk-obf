# 依赖与致谢

本篇列出 NK 依赖的开源项目、以及各模块的出处。

## 编译器与工具链

| 项目 | 用途 |
|---|---|
| LLVM 21 | 插件宿主。Pass 以 `libNk.so` 形式被 `-fpass-plugin` 加载，源码见 `src/` |
| Android NDK r29 | clang 21 / lld / sysroot / 目标 sysroot 头文件 |
| CMake + Ninja | 插件构建（目标名 `Nk`，产物 `libNk.so`） |

## 桌面端

| 项目 | 用途 |
|---|---|
| Python 3.10+ | 运行环境 |
| CustomTkinter | GUI 框架（深色卡片、圆角、滑动条） |
| PyInstaller | 打包成单文件 exe |
| `tools/xlverify.py` | 自实现 SM4（GB/T 32907）+ SM3（GB/T 32905）+ HMAC-SM3，纯 Python 无第三方依赖 |

## 手机端

| 项目 | 用途 |
|---|---|
| Jetpack Compose + Material3 | 全部界面 |
| Haze（`dev.chrisbanes.haze:haze:1.5.3` + `haze-materials`） | 液态玻璃模糊（API 31+ 走 `RenderEffect` 真模糊）；部分 GPU 上会崩，所以 vivo/OPPO/realme/一加/iqoo 默认关掉 |
| OkHttp | HTTP 客户端（GitHub API 与卡密服务） |
| kotlinx.coroutines | 上传/轮询/下载的并发与取消 |
| AndroidX DocumentFile / FileProvider | 目录选择与 APK 安装 |
| 自实现 SM4/SM3/HMAC（`XlVerify.kt`） | 与桌面端同算法的 Kotlin 版本 |

## 素材

- 界面皮肤（5 张背景 + Q 版头像 + 导航图标）：用户提供的开源皮肤包（`background_home/kernel/settings/superuser/system_module`、`grid_working_card_background`），已压缩为 WebP 内嵌，合计约 0.46 MB。
- 按钮音效（4 个 WAV）：项目内用 Python `wave` 现场合成（扫频/双段/方波/短促点击），无第三方音频素材。

## 参考与灵感

- OLLVM / OLLVM-Next 系列的混淆思路（字符串加密、控制流平坦化、虚假控制流、指令替换）。
- 公开的反调试/反注入检测清单（Frida、Xposed/LSPosed、Zygisk、eDBG/stackplz、QBDI、unicornTrace 等工具特征），用于 `ci/nk_ad.c` 的特征表。
- KernelSU / APatch 皮肤包的视觉风格，是手机端浅色主题的参考来源。

## 版本对照

| 组件 | 实际版本 | 被谁用 |
| --- | --- | --- |
| LLVM | 21.1.8 | 插件宿主；预编译包 `llvm-build-21.1.8` |
| Android NDK | r29 | clang 21 / lld / sysroot |
| Kotlin | 2.0.21 | 手机端全部代码 |
| Compose BOM | 2024.12.01 | UI（`ui` / `ui-graphics` / `material3`） |
| activity-compose | 1.9.3 | 单 Activity 容器 |
| lifecycle-viewmodel-compose | 2.8.7 | `BuildViewModel` |
| kotlinx-coroutines | 1.9.0 | 上传 / 轮询 / 下载 |
| Haze | 1.5.3 | 液态玻璃模糊 |
| OkHttp | 4.12.0 | GitHub API、卡密服务 |
| JVM target | 17 | 编译目标 |
| Python | 3.10+ | 桌面端运行时不需装，`NKBuild.py` 直接跑需要 |
| CustomTkinter | 5.x 系列 | 桌面端卡片 / 圆角 / 滑动条 |
| PyInstaller | onefile 模式 | 打包 13.35 MB 的 exe |

## 国密实现说明

两端的 SM4 / SM3 都是自实现，没有引第三方密码库：

| 算法 | 标准 | 用途 |
| --- | --- | --- |
| SM4（CBC 模式） | GB/T 32907 | 卡密请求体加密 |
| SM3 | GB/T 32905 | 摘要 |
| HMAC-SM3 | 按 SM3 构造 | 信封完整性校验 |

桌面端在 `tools/xlverify.py`，手机端在 `XlVerify.kt`，两边密文可以互解——改其中一边必须同步改另一边。

## 素材清单

| 文件 | 用途 | 体积 |
| --- | --- | --- |
| `bg_home.webp` / `bg_kernel.webp` / `bg_module.webp` / `bg_settings.webp` / `bg_superuser.webp` | 五页背景，`Crossfade` 切页换图 | 47.7 / 94.0 / 78.1 / 44.7 / 64.5 KB |
| `card_hero.webp` / `card_focus.webp` / `card_log.webp` | 首页主卡、焦点卡、日志框底图 | 41.7 / 41.7 / 59.9 KB |
| `mascot.webp` | Q 版形象 | 24.2 KB |
| `ic_nav_home/kernel/out/pre/set.png` | 五个导航图标 | 30.4 / 27.0 / 36.2 / 29.3 / 34.3 KB |
| `ic_launcher_fg.png` | 应用图标前景 | 188 KB |
| `sfx_click.wav` / `sfx_ok.wav` / `sfx_deny.wav` / `sfx_tick.wav` | 按钮音效（`SoundPool` 单流 + 55 ms 节流） | 4.9 / 16.4 / 11.5 / 2.5 KB |

图片全部来自「用户提供的开源皮肤包」，压成 WebP 内嵌进 APK；音频是项目内用 Python `wave` 现场合成的，没有外部音频素材。

## 二次分发注意

- 项目本体 MIT；皮肤素材按皮肤包原作者的条款，转发前先确认来源标注还在。
- 不要把 LLVM 或 NDK 打进自己的分发物里——本项目只在 CI 里现取，交付物只有那两个 GUI 与混淆结果。
- 卡密服务端代码不在此仓库，客户端只有校验与网络部分。

## 许可

本项目以 MIT 许可发布，见仓库根目录 `LICENSE`。第三方组件遵循各自许可。

## 授权（重要）

`src/`、`ci/`、`kit.tar.gz` 源自 **OLLVM-Next**（上游 AGPL-3.0），因此按 AGPL-3.0 分发；修改后对外分发（含以网络服务形式提供）必须公开对应源码。
`tools/`、`android/`、`docs/` 为本项目自写，按 MIT 分发。详见根目录 `LICENSE`。
