# 发布与下载

本篇讲从哪里下载、每个资产是什么、卡密系统怎么用以及怎么彻底删掉。

## 下载入口

| 产物 | 位置 | 大小 | 说明 |
|---|---|---|---|
| 手机端 APK | Release `v1.0` → `NKBuild-mobile-1.0-kami.apk` | 9.98 MB | Android 8.0+（`minSdk 26`），签名已固定，可直接覆盖升级 |
| 桌面端 EXE | Release `v1.0` → `NKBuild-1.0-win64.exe` | 13.35 MB | Windows x64 单文件，免安装 |
| 混淆后的示例二进制 | Release `nk-builds` → `lucifer` | 21,145,056 B | 每次构建自动覆盖（`--clobber`） |
| 工具链 | Release `nk-builds` → `nk-toolchain.tar.gz` | KB 级（只含 `source.properties` 与打包入口） | clang 21 + lld + sysroot 打包入口 |
| LLVM 预编译 | Release `llvm-build-21.1.8` → `llvm-build.tar.zst` | 232 MB | CI 缓存用，不占 artifact 配额 |

> Release 资产长期保留；Actions artifact 只留 **3 天**，别当下载源用。

## 卡密系统

### 现状：可以随便输入

服务端目前**只校验信封**（SM4-CBC 加密 + HMAC-SM3 校验 + 时间窗 + `check` 签名），不校验卡密真伪 —— 任意字符串都能通过，纯占位。界面上的「到期时间」来自服务器返回的 `msg.vip`（秒级时间戳），服务器没给就显示「永久」。

### 内置检查更新

设置页「⟳ 检查更新」读云端 `api.php?api=ini&app=…` 的 `version` 与本地 `versionName` 比对，不同就给出 `app_update_url`；读不到时退回到 GitHub 产物比对（取最近一次成功的 `build-apk` run 的 APK 并比较 `versionCode`）。

### 想彻底删掉卡密

**桌面端**（`tools/`）：

1. 删 `tools/xlverify.py`；
2. `tools/NKBuild.py` 里删掉 `import xlverify as XL`；
3. 删 `_build_license_bar()`、`lic_login()`、`lic_paste()`、`lic_unbind()`、`lic_ok()` 与 `_lic_apply/_lic_state/_lic_text/_lic_km` 字段；
4. `start()` 开头那句 `if not self.lic_ok(): …return` 删掉，`_drain()` 里的 `lic` / `licmsg` 分支一并删。

最省事的做法：把 `lic_ok()` 改成 `return True`，其余不动。

**手机端**（`android/`）：

1. 删 `app/src/main/java/com/nk/nkbuild/XlVerify.kt`；
2. `MainActivity.kt` 里删 `KamiGate` composable 与 `App()` 中的 `if (!st.licOk) KamiGate(st) { … }`；
3. `BuildViewModel.kt` 里删 `licAuto/licLogin/licUnbind` 三个方法与 `UiState` 的 `licOk/licText/licBusy` 字段；
4. `Prefs.kt` 里 `kami/licVip` 可留可删（留着无害）。

## 版本号约定

- 手机端：对外 `versionName` 固定 `1.0`，只递增 `versionCode`（当前 19）——避免每次改文案。
- 桌面端：文件名带版本号，`NKBuild-<版本>-win64.exe`（ASCII 名，中文名会被 GitHub 剥掉）。
- 产物：`<out_name>`（无扩展名，默认 `lucifer`），由 CI 上传到 Release `nk-builds` 的 `lucifer` 资产。

## 校验值与自检

Release 页面每个资产下都会显示 SHA256，拿不准时自己算一遍：

```powershell
# Windows
certutil -hashfile .\NKBuild-1.0-win64.exe SHA256
```

```bash
# Android / Linux / macOS
sha256sum NKBuild-mobile-1.0-kami.apk
```

APK 的签名摘要用 `apksigner` 核对，公钥摘要应与 `android/keystore/nkbuild.p12` 一致：

```bash
apksigner verify --print-certs NKBuild-mobile-1.0-kami.apk | grep SHA-256
```

混淆产物没有对外校验值——每次构建都会变（编入 commit 哈希、品牌串、`.comment` 段）。要留档就自己 `sha256sum <产物>` 记一笔；想确认手上是不是最新一次，先看大小（最近一次是 21,145,056 B），再到 Release `nk-builds` 的更新时间对一下。

## 资产该下哪个

| 你的场景 | 下这个 | 不用下 |
| --- | --- | --- |
| 只想编自己的工程，机器是 Windows | `NKBuild-1.0-win64.exe` | 其余全部 |
| 只有手机 | `NKBuild-mobile-1.0-kami.apk` | 其余全部 |
| 想在 CI 里跑、不装客户端 | 不用下载，直接 `workflow_dispatch` | 其余全部 |
| 想验证插件效果 | `nk-builds` 的 `lucifer` | 两个 GUI |
| 自己搭 runner / 复现缓存 | `llvm-build.tar.zst` | 两个 GUI |

## 发布流程（自己发一版）

1. 改 `android/app/build.gradle.kts` 的 `versionCode`（+1），`versionName` 保持 `1.0`。
2. push 到 `android/**` 触发 `build-apk.yml`，签名固定 `android/keystore/nkbuild.p12`。
3. 桌面端跑 `pyinstaller tools/NKBuild.spec`，产物改名 `NKBuild-1.0-win64.exe`。
4. `gh release create v1.0 <两个文件> --repo 3495866893/nk-obf`；tag 已存在时用 `gh release upload v1.0 <文件> --clobber`。
5. 复核：`gh release view v1.0 --repo 3495866893/nk-obf`，两个资产大小应是 9.98 MB 与 13.35 MB。

`nk-builds` 是滚动 tag，每次构建 `--clobber` 覆盖同名资产，不留历史版本；要留档就自己另存。

## 卡密接口返回码

| 返回 | 含义 | 处理 |
| --- | --- | --- |
| `code=0` | 信封校验通过 | 进主界面，`msg.vip` 即到期秒 |
| `code=179` | 账号未激活（老账号体系遗留） | 发布版 v1.0 已去掉账号，直接用卡密登 |
| `msg.vip=4102243200` | 到期时间戳（秒） | 界面上换算成年月日，不是错误 |
| 网络失败 | 域名 `yz.dujiuyue.cn` 不可达 | 检查网络；域名换过就看 `XlVerify.kt` 的 `HOSTS` |
