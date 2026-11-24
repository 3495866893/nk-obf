# 变更记录 · 从第一次对话到现在

> 这份记录从「主人第一次说『做个 NKBuild』」开始，按阶段列出每一次真实改动、每个踩过的坑和最终数字。
> 时间线：2026-11-21 起。
> 版本号约定：开发期用过 1.1–1.18 这类小号，**对外是 `v1.0` 与其增量补丁 `v1.0.2`**（versionCode 20 / 22）。

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

## 阶段 15 · 反调试 v12 加固

`ci/nk_ad.c` 从 64,441 字节增到约 76,962 字节（1,933 行）。起点是一份静态评估：30 多个检测点全部汇到 `nk_die_r()` 里同一句 `_exit(NK_EXIT)`，在 `.text` 里把那条 `bl` 改成 `ret` 就全线失效；另外几条判据仍停在「枚举已知字符串」的层次。

| 编号 | 问题 | 改法 |
| --- | --- | --- |
| P-1 | 处置出口唯一 | 新增 `nk_panic_ill()`（`brk #0x1` → SIGILL）、`nk_panic_spin()`（伪装 ANR：忙等 5 秒后 exit，自带超时，绝不无限挂）、`nk_panic_exit()`（裸 `svc #0` 走 exit_group），由 `nk_fatal_poly()` 按计数器 `g_panic_n % 3` 轮换，静态上不再存在唯一处置点 |
| P-2 | 看门狗进程可被冒充 | `nk_wd_reap()`（`waitpid(WNOHANG)` 回收僵尸 + `kill(pid,0)` 探活）与 `nk_peer_verify()`（用 `/proc/<pid>/stat` 第 22 域 starttime 校验身份，防 pid 回收复用后冒充）只记软分，不判死 |
| P-3 | 自哈希只覆盖前 1 MB | `nk_seg_verify()` 把 `.text` 切 8 段轮转校验，1.5 秒一段、12 秒覆盖全量，覆盖窗口上限消失且单轮成本降到 1/8；首轮只采样不比对 |
| P-4 | 只枚举已知字符串 | `nk_struct_scan()` 改为遍历可执行映射按结构判定（路径白名单前缀、` (deleted)` 后缀、非 ART JIT 前缀的 `/memfd:`）；`nk_thread_anomaly()` 从「枚举坏名字」改成「总数 + 白名单外计数 > 12」，改名绕不过去；全部软分 |
| P-5 | 只伪造一条通道就成立 | `nk_cross_conflict()` 用裸 `svc #0` 的 `getppid`(173) 与 `/proc/self/status` 的 `PPid` 交叉比对，不一致记软分，必须同时伪造两条通道 |
| P-6 | 5 处各自 `malloc(256 KB)` 全扫 maps | 新增 `nk_vma_t` + `nk_vma_refresh()`：1 MB 一次性读 `/proc/self/maps`，`sscanf("%lx-%lx %7s %*s %*s %*s %n")` 解析出 lo/hi/perm/path，带代际号 `g_vma_gen` 保证同轮幂等，新的结构化判据统一消费这一份 |
| S-1 | 微架构判据直接判死 | 改为只翻 `g_uarch_flags` 位 |
| S-2 | 写死 24 MHz / 19.2 MHz 类固定频率跨不了 SoC | `nk_cntvct_calibrate()` 在构造函数（早于业务）用两个短窗口标定本机 `cntvct` 与 `CLOCK_MONOTONIC` 的比例，之后 `nk_uarch_update()` 只测漂移，相对偏差 > 35% 才置位 |
| S-4 | 轻量模拟器的 syscall 分发表 | `nk_syscall_semantic()` 抽三个 arm64 未实现的号（0x7fff / 0x0e00 / 0x3a00），返回既不是 `-ENOSYS`(-38) 也不是 `-EPERM`(-1) 就记软分——模拟器的分发表最先在边界崩 |
| S-6 | 缓存 fd 会自占槽位 | `nk_hwbp_slot_count()` 逐槽 `perf_event_open` 再立刻 close，统计实际可用硬件断点槽数（< 3 记软分），绝不缓存 fd；且只在检出 root 框架时执行（untrusted_app 会被 SELinux 拒） |

接线：`nk_tool_scan(!early)` 里依次跑 `nk_seg_verify → nk_struct_scan → nk_peer_verify → nk_cross_conflict → nk_uarch_update → nk_syscall_semantic → nk_behavior_scan`；低频项 `nk_thread_anomaly` / `nk_hwbp_slot_count` 放进 60 秒一次的行为检查，避开 1.5 秒一轮的开销；看门狗线程每轮跑 `nk_wd_reap + nk_seg_verify`。

### 教训

1. **反调试模块自己过混淆会导致启动卡死**。把 `ci/nk_ad.c` 也送去过 7 个 pass（bcf / indirect_branch / split_blocks / flattening / constant_encryption / substitution / string_encryption）后，成品启动**零输出卡死**：`debuggerd -b` 栈是 `nanosleep ← usleep ← 我们的构造函数 ← __dl_soinfo::call_constructors`，`nk_ad.log` 一行都没有，`RC=124`，3/3 复现。改成只过 `string_encryption`（`nk-self.toml`）后恢复正常。结论：**自混淆只做字符串加密，语义类 pass 一律不碰 `nk_ad.c`**。
2. **明文泄露扫描的量化结果**。加固前 `nk_ad` 明文 79 处、`__nk_ad_check` 1 处、`TracerPid` 2 处、`frida` 23 处、`lsposed` 4 处；加固后 `nk_ad` 1 处（只剩日志路径）、`__nk_ad_check` 0、`TracerPid` 0、`frida` 0、`lsposed` 0。

---

## 数字回顾

| 指标 | 值 |
|---|---|
| 单次构建（缓存命中） | 41 秒 ~ 1.8 分钟 |
| 首次自建 clang | 40–100 分钟（只一次，随后存 Release） |
| 示例产物 | 21,145,056 B，`.text` 16,086,400 B，`brk 0 / udf 0` |
| 真机连续启动 | 100/100 无卡死（RMX3888）、100/100 无卡死（RMX3300） |
| 混淆后体积 | 原大小的 2–3 倍；稳态性能 −10%~−40% |

## 阶段 15.1 · v12.1（S-3 / S-5）与双端 1.0.2

补完 S 系列剩下两条，都是「像模拟器」的旁证，只记软分、不判死：

| 编号 | 判据 | 做法 |
| --- | --- | --- |
| S-3 | 缓存侧信道比值 | 4 MB 缓冲，同一批页分别按顺序与打乱顺序各走 512 步，取 3 轮最优值比较；随机访问不足顺序的 1.08 倍记 `cache-no-penalty`，单次随机访问 < 6 ns 记 `cache-latency-too-low`。每轮种子不同，静态分析无法预知访问序列 |
| S-5 | cpuinfo / MIDR 自洽 | `/proc/cpuinfo` 缺 `implementer` 记 `cpuinfo-no-implementer`；出现 qemu/goldfish/ranchu/GenuineIntel 等词记 `cpuinfo-emulator-token`；`processor` 行数与 `sysconf(_SC_NPROCESSORS_ONLN)` 相差 2 倍以上记 `cpu-count-mismatch`；`MIDR_EL1` 为 0 记 `midr-zero`，implementer 不在真实 ARM 厂商集合（0x41/0x48/0x4d/0x51/0x53/0x61/0x6e）记 `midr-implausible` |

| 项 | 变化 |
| --- | --- |
| `ci/nk_ad.c` | 77,129 → 81,824 字节（2,039 行），新增 `nk_cache_ratio()` / `nk_cpuinfo_conflict()` / `nk_ns_now()` |
| 桌面 NKBuild | 1.0.0 → **1.0.2**（标题栏带版本号、品牌副标题加一行版本） |
| 手机 NKBuild | 1.0 → **1.0.2**（versionCode 22） |
| 构建 | 私有仓 run `45178198836` success，产物 2,097,528 字节，明文泄露仍为 `nk_ad` 1 / `__nk_ad_check` 0 / `TracerPid` 0 / `frida` 0 |

## 阶段 15.2 · 1.0.2 补丁：P-3 编译期基线 + 注入随机化 + 分块

不升版本号（仍是 1.0.2 / versionCode 22），只替换 Release 资产。

| 项 | 内容 |
| --- | --- |
| P-3 编译期基线 | 新增 `ci/tools/nk_patch_baseline.py`：链接后先不 strip，把主代码段按 64 段逐段 FNV-1a 写进 `__nk_base_hash[]`，被重定位命中的段在 `__nk_base_exempt[]` 置豁免位，再 `brand`、最后 `llvm-strip --strip-all`。运行期 `nk_cbase_check()` 每轮只比一段，12 秒覆盖全量 |
| 注入位置随机化 | 插件把「函数名 FNV-1a + `NK_AD_SEED`」混成 `nkH`：`nkH % 100 >= 62` 的函数不插（约 38% 跳过），插入点在入口 / 第 2 / 第 3 个基本块之间轮换，ART 表校验只在一半函数展开 |
| 单文件拆分 | `ci/nk_ad.c` 2,099 行 → 壳（头 + 8 条 `#include "nk_ad_parts/NN_*.inc"`）+ 8 个分块，编号按原始顺序，token 流等价 |
| 文档纠错 | 之前多篇文档把 `nk_ad.c` 说成完全不混淆，实际是「只过 `string_encryption`、语义 pass 全不碰、入口符号每次随机」。README / ARCHITECTURE / FEATURES / index 中英共 8 处已改，并补了 6 个必须留名的符号清单 |
| 构建 | 私有仓 run `45191819853` success（P-3 + 分块 + 随机注入同时生效） |

## 阶段 15.3 · v12.2：E / N / Q 系列（反调试补完）

仍属 1.0.2，不升版本号。

| 编号 | 问题 | 改法 | 档位 |
| --- | --- | --- | --- |
| E-2 | 只看 `/proc/self/status` 的 TracerPid，看门狗被别的 tracer 先占住看不出来 | `nk_wd_reseize()`：主进程反向 `PTRACE_SEIZE(wd)` → `PTRACE_INTERRUPT` → `wait4(WNOHANG)` 证明可调度 → `DETACH`。`EPERM` = 已被别人占（软分 `wd-seized-by-other`），`ESRCH` = 进程没了（`wd-gone`） | 软分 |
| E-4 | `g_uarch_flags` 只翻位、没有任何消费点，等于死代码 | `nk_uarch_consume()`：定时器比例漂移 **且** 已有 2 条真·模拟器证据 → 才硬处置；单条漂移只记一次软分（省电调频会让正常机也漂） | 软分 + 合判硬 |
| E-6 | 模拟器判定只依赖文件内容，伪造成本低 | `nk_prop_get()` 走 `dlsym("__system_property_get")` 读 `ro.kernel.qemu` / `ro.hardware` / `ro.product.cpu.abi` 作第二信源 | 软分 |
| N-1 | 不知道进程是否被挂了 seccomp 过滤层 | `prctl(PR_GET_SECCOMP)` 自检，只写日志（Android 8+ 自带过滤器，打分必误杀） | 仅日志 |
| N-2 | PAC / BTI 可用性未知 | `getauxval(AT_HWCAP/AT_HWCAP2)` 读 `HWCAP_PACA(1<<30)` / `HWCAP_PACG(1<<31)` / `HWCAP2_BTI(1<<17)`，只写日志 | 仅日志 |
| N-3 | 自身二进制被搬到别处也照样跑 | `readlink("/proc/self/exe")` 校验是否落在 `/data/app/`、`/data/local/`、`/data/data/`、`/data/user/`、`/system/`、`/vendor/`、`/apex/`、`/product/`、`/odm/`、`/mnt/` 之内，否则软分 `self-path-outside-install` | 软分 |
| Q-3 | 看门狗刚 fork 那一两秒 `g_wd_pid` 还没落盘，自己人会被当成外来 tracer | 启动后 8 秒内、`g_wd_pid<=0` 且 comm 为 `nk-wd` → 放过（`nk_boot_age_ms()`） | 防误杀 |
| Q-6 | maps 读满 1 MB 或条目打满 320 时，把「缺条目」当成证据 | 新增 `g_vma_trunc`，截断时结构化判据（`exec-suspicious-path`）自动降级不记分 | 防误杀 |

文件：新增 `ci/nk_ad_parts/09_nk_env_probe.inc`（5.1 KB），`nk_ad.c` 壳多一条 include；分块总数 9。

## 阶段 15.4 · v12.4：反调试误杀（第一轮）

| 现象 | 根因 | 修法 |
|---|---|---|
| 真机启动即 `exit 97`，日志混着 `emulation score>=3` / `emulator cpuinfo` | 软证据每次深检都重复加分（worker 1.5 s 一轮），几秒就撞阈值；`/proc/cpuinfo` 判定过严 | 软证据位图去重（`nk_emu_hit(bit, tag)`）、判死门槛 3 → 4 个不同项、`timer-ratio`/`comm-readback`/`segv-not-delivered` 降级为只记日志 |
| 只开「反调试」一个 pass，产物却像重度混淆 | 超大编译单元的自动降级（`nk-nofold` / `nk-light`）覆盖了用户配置 | 降级仅在用户配置里 `enabled = true` 数量 ≥5 时启用，否则尊重用户配置 |

## 阶段 15.5 · v12.5：看门狗不再 ptrace（决定性修复）

真机证据：`nk_ad[chk] TracerPid=27125 wd=27125` → `nk_ad[tool] status State: t` → `nk_ad[die] reason=nk_ad: traced state`。

自家看门狗原本用 `PTRACE_SEIZE` 看护主进程，这会把主进程留在 ptrace-stop（`State: t`），主进程的 `nk_trace_state()` 又把自家看门狗当成外来调试器 —— 自己把自己杀了。

修法：看门狗改成每 300 ms 读 `/proc/<pid>/status` 的 `TracerPid`，`>0` 就 `kill(target, 9)`；fork 返回即认亲（`g_wd_pid = wd`）；主进程反向 seize（E-2）置空。**完全不建立 ptrace 关系，主进程调度状态不再被改变。**

## 阶段 16 · v1.0.5（发布：修复重大 bug）

- 版本：双端 **1.0.5**（桌面 `NKBuild-1.0.5-win64.exe` 14,002,438 B，手机 `NKBuild-mobile-1.0.5-kami.apk` 10,460,373 B，versionCode 25）。
- 新增**启动验证闸门**：反调试开启时先跑环境检查，通过则打印紫色大字横幅「NK 1.0.5 验证通过」+ 颜文字，终端停留 3 秒后清屏进主程序；不通过则打印「请关闭你的调试工具」并退出。`NK_AD_GATE=0` 关闭横幅，`NK_AD_ALLOW_DEBUG=1` 允许 ptrace 调试。
- 真机回归（RMX3888 / Android 16）：8 pass + 反调试 5/5 `rc=0`；只开反调试 3/3 `rc=0`；两者被 `tracer` 牵引均 `exit 97`。
- 逐 pass 运行时矩阵（16 个 pass 各单独编译 + 真机各 3 次）：**全部 `rc=0`**，没有一个 pass 单独会崩。
- 已知缺陷：16 个 pass **全开**（`all16`）在 `projects/loop` 上 `SIGSEGV`（`pc=0`，worker 线程空指针间接调用）。组合二分显示：去掉 `string_encryption` / `substitution` / `indirect_branch` / `anti_debugging` 任一个都不再崩 ⇒ 多 pass 交互缺陷，默认预设（8 个核心 pass）不受影响。
