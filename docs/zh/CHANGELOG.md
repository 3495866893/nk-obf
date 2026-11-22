# 变更记录 · 从第一次对话到现在

> 这份记录从「主人第一次说『做个 NKBuild』」开始，按阶段列出每一次真实改动、每个踩过的坑和最终数字。
> 时间线：2026-11-21 起。
> 版本号约定：开发期用过 1.1–1.18 这类小号，**对外只有发布版 `v1.0`** 一个版本号（versionCode 20）。

---

## 阶段 0 · 起点：一个能把源码丢上去、拿回混淆二进制的工具

第一天的需求：桌面 GUI —— 选项目目录、上传到 GitHub、触发 Actions、拿回被**旧代号插件**（后来的 NK）混淆的 aarch64 二进制，混淆与反调试可勾选。

- 落地 `tools/NKBuild.py`：customtkinter 四卡片界面（源码 / 工程与产物 / 反混淆开关 / 开始与日志），`Github` 类负责 blob → tree → commit → dispatch → 轮询 run → 下 artifact。
- 混淆侧：`kit.tar.gz` 里的 LLVM 21 Pass 插件（当时叫 `lib<旧代号>.so`），用 `-fpass-plugin` 挂到 NDK r29 的 clang 上，配置走 `ci/*.toml`。
- 第一版就从 17.6 分钟压到 1.8 分钟：apt 依赖改成按需检查（7.5 分钟 → 0.02 分钟）、smoke test 默认跳过（4 分钟 → 0）、插件与 clang 走缓存。

## 阶段 1 · 反调试让程序「一启动就静默退出」

现象：开了 `anti_debugging` 的包启动后无输出退出。

- 用自建探针测得：**第 1 次 `PTRACE_TRACEME` 返回 0，第 2 次就 `-1 EPERM`** —— 而老实现是「每个函数入口 + 全局构造器」各插一次，所以必然第 2 次就判定「被调试」并自杀。
- KernelSU 环境下 `su` 里连第 1 次都 `EPERM`。
- 修法：把 AArch64 分支整体换成 **`TracerPid` 探针**（open `/proc/self/status` → 读 1023 B → `strstr "TracerPid:"` → `strtol`），不再调 `ptrace`。
- 同时定位「静默退出」的尾巴：老实现的暴力退出里塞了 **Q32 混沌死循环**，所以超时工具看到的是 `rc=124/137` 而不是退出码。

## 阶段 2 · 自建运行时看门狗 `ci/nk_ad.c`

从「插件插检测」升级成 **插件插调用点 + C 模块装逻辑**：

- LLVM 层只在每个函数入口插 `call __nk_ad_check()`（IR 级），逻辑全在 `nk_ad.c`（用 `-x c` 单独编译，避免被插件自己混淆出 `.L_vXXXX` 未定义符号）。
- 检测项：`TracerPid` 交叉校验、全线程扫描（`/proc/self/task` + `getdents64`）、maps 快照差分、libc 序言改写扫描（只认跳出模块的 `B/BR/BLR/ldr+br`，避开 bionic thunk 误报）、自身 `.text` FNV 哈希、`prctl(PR_SET_DUMPABLE,0)`、纯 `svc #0` 直连系统调用（openat 56 / read 63 / close 57 / write 64 / getdents64 61）。
- 读不到就放行（fail-open），命中 `_exit(97)`；调试输出由 `NK_AD_DEBUG=1` 打开，日志 `/data/local/tmp/nk_ad.log`。
- 真机验证（realme RMX3300，Android 15，KernelSU）：正常启动打印自己的界面、`rc=255`；被 `nk_spawn` ptrace 时 **exit 97**。

## 阶段 3 · GUI 该有的东西

- 16 个 pass 全部可选，`anti_hooking / anti_class_dump / objc` 标灰（一试就崩）。
- 终止按钮（`threading.Event` 贯穿上传 / 等 run / 轮询三阶段，同时调 GitHub 取消接口）、ETA（`max(120, 120+20×pass+min(900, 20×MB))`）、耗时显示、失败时列出**具体哪一步红了**并给 run 链接。
- 输出目录 5 级回退（桌面 / 文档 / `~` / LOCALAPPDATA / TEMP），每级先写探针文件再落盘。

## 阶段 4 · Frida 与 Actions 配额

- 想装 frida-server 做真机检测验证：拿到的是 `win_arm64` 轮子（本机 AMD64 跑不了），frida-server 又必须 root 才能起；设备当时换成了一台未 root 的 vivo。
- 顺手发现 Actions 存储爆了：139 个 artifact / 5.8 GB（`llvm-build` 每次 225 MB × 17 份），清到 225 MB，并把清理 workflow 改成「保留 `llvm-build`、`ndk-*-cache`、`<旧名>-plugin-*`，总量 >300 MB 才删」。

## 阶段 5 · 工作区被误删，全量恢复

- 整个 `ds工作区` 被删除。恢复路径：旧 PAT 已失效 → 从 `%APPDATA%\NKBuild\config.json` 里翻到还能用的 token → `tarball/main` 拉全量快照（168 个文件）→ 解出 `kit.tar.gz`（140 条目）。
- **`NKBuild.py` 从未进过仓库**，只能重写：四卡片界面 + `PASS_SPEC` 16 项 + 预设分段按钮 + 终止 + ETA + 5 级输出回退，`py_compile` 与 GitHub 层冒烟都过。
- 期间把 CI 的 `NDK_DIR: unbound variable`、`guard (cache required)` 快速失败、`if:` 重复键导致 dispatch 422 全部修掉。

## 阶段 6 · 手机版 GUI（v1.1 → v1.2）

- Kotlin + Compose：源码扫描（含 `.h/.hpp/资源` 上传）、pass 勾选、`ci/ui-<工程>.toml` 本地生成后上传、进度与 ETA、日志框、产物自动存 `Download/NKBuild` 并一键安装。
- 底部导航做成**液态玻璃胶囊**（Haze `hazeEffect` + 滑动选择块），4 个 tab：构建 / 混淆 / 产物 / 设置。
- 首轮编译失败 3 个错：`DIRECTORY_DOWNLOAD` 少个 S、缺 `animateFloat` import、`@Composable` 在非 composable 上下文调用 —— 全部修掉后 v1.2 出包。

## 阶段 7 · 「上传超过 1 MB 会失败」的疑云

- 实测直接打 `POST /git/blobs`：1 MB、2 MB、5 MB、20 MB **全部 201** ⇒ blob 接口没有 1 MB 限制（上限 100 MB）。
- 真正 1 MB 限制在 `PUT /contents`，而工具里只有「写 `ci/ui-<proj>.toml`」一处用它。给 blob 加 4 次退避重试，>1 MB 打日志，>70 MB 跳过。
- 顺手解决装不上的问题：`INSTALL_FAILED_UPDATE_INCOMPATIBLE` 的根因是 GitHub runner 每次新生成 debug keystore → 用 openssl 生成固定 PKCS12（`android/keystore/nkbuild.p12`）并写进 `signingConfigs`。

## 阶段 8 · 预检、鱼、皮肤（v1.3 → v1.4.6）

- v1.3：新增「预检」页 —— 上传前扫出会崩的文件（>70 MB 必崩、编译单元名带空格/中文必崩、>45 MB 很可能崩、图片字体等资源只记「注意」、>60 个编译单元撞限流），并在开始编译前直接拦截。
- 背景先做过一版自绘「蓝色大肥鱼」（矢量、带摆尾吐泡），后来主人给了 8 张 KernelSU 风格粉色皮肤图 ⇒ 删鱼，改成**每页一张背景 + 切页交叉淡入 + 一个 Q 版头像**，UI 从深紫玻璃整改成浅色（卡片 0xF2 白、accent 0xFFD2557F）。
- 导航图标直接从皮肤图裁头像（显著性找头顶 + 圆形蒙版 + 提饱和），最后统一换成 Q 版粉发女孩；应用图标用 adaptive-icon（`ic_launcher_fg` + 粉底 `#FFF3F7`）。
- 一路上修的编译错：`Brush.verticalGradient(...)` 少一个右括号、`quadTo` 不存在（改 `lineTo` 折线）、`Box(weight)` 没给底部留 96 dp 导致卡片被导航压住。

## 阶段 9 · 切后台任务丢失（断点续接 + 任务历史）

- 现象：切出去再回来，编译任务没了；服务端也查不到那次上传（`viewModelScope` 随进程被杀一起死）。
- 方案：`BuildService` 前台通知保活 + `Prefs` 里记 `runId / jobProj / jobOut / jobEta / jobStart`，启动时 `resume()` 只接管「已拿到 runId」的任务（上传阶段被打断就明确告诉用户「上次上传被打断，点开始编译再来一次」）。
- 之后又加「任务历史」：每次 dispatch 把 `(runId, 工程, 产物, 时间, 文件, 是否已下载)` 存进 `org.json`（留 30 条），在「产物」页点一条就能**重新下载**。

## 阶段 10 · 真正的硬仗：编译链

按时间顺序：

1. **`.L_v` 未定义临时符号**（19–22 条）——二分到 `string_encryption`；根因是 `StringEncryption.cpp:542` 把解密缓冲区设成 `GeneralDynamicTLSModel`，Android triple 非空必走 TLS，汇编只剩 `adrp x8, .L_v…` 引用没有定义。改成沿用原变量的 `getThreadLocalMode()`，并把 4 处继承 `GV->getLinkage()` 的全局量改成 `InternalLinkage`。
2. **`duplicate symbol: cJSON_*`** —— `SRC_FILES` 里带了被 `#include` 的 `.c`；两端 GUI 现在扫描时自动剔除「被别人 include 的源文件」。
3. **`relocation refers to a discarded section: .text.startup`**（11 条）——不是 `--gc-sections`，是 COMDAT；在 `Utils.cpp toObfuscate()` 里跳过带 comdat / linkonce_odr / weak 的函数，并给 `BASE` 加 `-fno-asynchronous-unwind-tables -fno-unwind-tables`。
4. **`vkCreateAndroidSurfaceKHR` 未声明** —— 缺 `-DVK_USE_PLATFORM_ANDROID_KHR`（第一次补丁把宏塞进 `-D_FORTIFY_SOURCE=2` 同一个字符串里，等于没定义）。
5. **PhysX 要求 `NDEBUG`/`_DEBUG` 二选一** —— 补 `-DNDEBUG`。
6. **链接 `posix_spawn failed`** —— 编译用的 `CF` 数组过滤掉了 `--ld-path`，链接行要补回来。
7. **`build lucifer` 被 SIGTERM（exit 143）** —— 不是超时也不是 oomd：`mem_avail` 只剩 **119–398 MB**。关 systemd-oomd、加 16 GB swap、`overcommit_memory=1` 都没救；`-O1` 也没救。
8. **大 TU 分级降级** —— 预处理 >800 KB 的单元自动跳过 `flattening`（保留其余 7 个 pass），内存立刻回落到 4–7 GB，`imgui.cpp`（预处理 3.0 MB）37–51 秒编过，全量 17 个 TU 一次通过，产物 21,145,056 B。
9. **运行时报 `Illegal instruction`** —— 旧产物里有 **12 处 `brk`**（`flattening` 把 switch 兜底标成 unreachable，后端照规矩落成 `brk #0xf000`）；跳过 flattening 后 **brk 0 / udf 0**。
10. **（v11 之后）自己把自己杀了** —— `nk_text_wlock()` 把文本改成 RWX 后，下一轮「文本可写 = 有 uprobe」判定会命中自己；加 `g_text_rwx_self` 标记，默认还关掉 wlock。

## 阶段 11 · 卡密授权（XL 云验证）

- 协议逐行从 `xl_verify.h` + `gm.h` 抠出来：`data = Base64(IV16 + SM4-CBC(GMKEY,IV, 8B随机‖4B时间戳BE‖plain) + HMAC-SM3(GMKEY, IV‖ct))`，响应同样解包后**先丢 12 字节**才是 JSON，再校 `code==200`、`|time-now|<=30`、`check==md5(str(time)+APPKEY+value)`。
- 桌面 `tools/xlverify.py`（纯 Python，无依赖）：SM4 表 + SM3 + HMAC-SM3 + CBC + 信封封/解 + 设备码 + 双 host 回退。国标向量 `681EDF34D206965E86B3E94F536E4246`、`SM3("abc")` 全部对上；真服务器用假卡密回 `code=149`，证明链路通。
- 手机 `XlVerify.kt` 用 Kotlin 重写同一套算法（`value` 用 `toUnsignedString` 顶掉 Java 没有的 `%u`），并用 Python 逐字节镜像校验一致。
- 界面：桌面顶部授权条（状态点 + 卡密 + 登录/粘贴/解绑），手机全屏卡密门；后来又按主人要求**去掉账号体系只留卡密**，并修 `vip` 在 `msg` 里且是字符串导致的「到期时间永远永久」。
- 加音效（4 个合成 WAV：click / ok / deny / tick，`SoundPool` 单流不叠）+ 震感 + 左上角自动消失提示条。

## 阶段 12 · 反调试强化 v7 → v11.1

按主人给的三份评估报告（`mj.md`、`mj (3).md`、`mj (4).md`、`mj (5).md`）逐条落地：

| 版本 | 加了什么 |
|---|---|
| v7 | 12 项模拟环境检测（qemu 属性、`gGoldfish`、`/dev/qemu_pipe`、abort 消息比对、MAGIC 值、maps 里的 qemu 段…），软分累计 ≥3 才杀 |
| v8 | 工具指纹：maps 20 项黑名单、线程 comm、`/proc/net/tcp` 端口 27042/27043/27052、`/proc/net/unix` 的 `@frida`、frida 文件、`State: t`、uprobe BRK 改写扫描 |
| v10 | JNI 函数表落点、匿名可执行段、`svc #0` 自检、`/data/adb` 分级、perf 硬件断点槽、多模块 `.text` 哈希、artseek 兜底 |
| v10.1/10.2 | `g_text_rwx_self`：自己 mprotect 出来的 RWX 不再算攻击 |
| v11 | 放过 ART JIT 的 `/memfd:jit-cache`、哈希名单砍成 主模块+libc+libm+libdl、基线延后到首次深检、`errno` 改 `-fd`、wlock 默认关、JNI 降软分、maps `%n` 解析、`/data/adb` 存在即算 root |
| v11.1 | JIT 前缀精确匹配、JNI 软分只记一次、`g_tm_ready` 原子三态、删掉 init 里的冗余采样 |

## 阶段 13 · 配额治理与产物改走 Release

- `llvm-build` 每跑一次就上传一份，堆到 **289 个 artifact / 24.4 GiB**，直接撞爆配额（`Failed to CreateArtifact: Artifact storage quota has been hit`），编译一步都没跑就失败。
- 清理到 **16.8 MiB**；workflow 里删掉 `llvm-build` / `ndk-cache` 的上传，改成「clang 只在需要时重建 + 存成 Release 资产 `llvm-build.tar.zst`（232.3 MB，不占配额）」；artifact 保留期改成 3 天。
- 产物发布也改成 Release：`publish binary to release`（tag `nk-builds`，`gh release upload --clobber`），APK 同样走 Release —— 因为配额重算要 6–12 小时，等不起。

## 阶段 14 · 改名、双语文档、公开

- **全量改名**：早期代号 → `NK`。kit 目录 `nk/`（95 个文件）、产物 `libNk.so`、脚本 `ci/build-nk.sh`、环境变量 `NK` / `NK_CONFIG`，仓库 `nk-obf`。
- 文档：`docs/zh/` 与 `docs/en/` 各 8 篇（index / FEATURES / USAGE / ARCHITECTURE / COMPATIBILITY / CHANGELOG / RELEASES / CREDITS）+ 本文件 + 双语 `README.md` / `README.en.md` + 保姆级部署教程 `DEPLOY.md`。
- 两个子智能体逐篇校对，再用脚本机械复核：围栏成对、标题无跳级、相对链接全部可解析、旧名命中 0、无乱码、无遗留待办标记。
- **授权修正**：`src/`、`ci/`、`kit.tar.gz` 源自 **OLLVM-Next（AGPL-3.0）**，原来挂 MIT 是错的；改成「插件部分 AGPL-3.0 + 双端 GUI 与文档 MIT」，`CREDITS` 两边补授权节。
- 发布 `v1.0`：`NKBuild-mobile-1.0-kami.apk` + `NKBuild-1.0-win64.exe`，手机上显示 `v1.0（versionCode 20）`，关于页大字号 `NK / OLLVM-NEXT / 新一代混淆`。
- 公开前自查：仓库内**没有任何真实 PAT**（只有文档里的 `ghp_xxxx…` 占位），删掉私人工程目录，示例签名口令与 XL 测试密钥在 `CREDITS` 里注明「仅供示例，自行更换」。

---

## 数字回顾

| 指标 | 值 |
|---|---|
| 单次构建（缓存命中） | 41 秒 ~ 1.8 分钟 |
| 首次自建 clang | 40–100 分钟（只一次，随后存 Release） |
| 示例产物 | 21,145,056 B，`.text` 16,086,400 B，`brk 0 / udf 0` |
| 真机连续启动 | 100/100 无卡死（RMX3888）、100/100 无卡死（RMX3300） |
| 混淆后体积 | 原大小的 2–3 倍；稳态性能 −10%~−40% |
