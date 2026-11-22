# 变更记录 · 从第一行代码到 v1.0

本篇按时间顺序记录 NK 从「插件跑不起来」到「双端 GUI + 发布版」的全过程。每条都写清**症状 → 根因 → 修法 → 验证数据**，便于以后回归排查。

代号说明：2026-11-23 全量改名为 `NK`（仓库 `nk-obf`，插件 `libNk.so`），旧名与新名的逐项对照见阶段 G。手机端阶段 F 表里的 1.1–1.8.1 是开发期迭代号，对外只有发布版 **v1.0** 这一个版本号。

---

## 阶段 A · 让插件能编出可链接的产物（2026-11-21）

### A1 `Undefined temporary symbol .L_v…`

- **症状**：开了 `string_encryption` 的工程全部编译失败，报错是几十条 `error: Undefined temporary symbol .L_v<16 位十六进制>`，随后 `fatal error: too many errors emitted`。
- **定位**：先用全关配置（`ci/plain.toml`）编译通过 ⇒ 说明是某个 pass 造出来的；再对 8 个核心 pass 做二分：`only-str` 失败、`only-const` / `only-sub` / `only-bcf` 通过 ⇒ 唯一元凶是 `string_encryption`。
- **根因**：`StringEncryption.cpp:542-544`
  ```cpp
  GlobalValue::ThreadLocalMode tlsMode = M->getTargetTriple().empty()
      ? GlobalValue::NotThreadLocal : GlobalValue::GeneralDynamicTLSModel;
  ```
  Android triple 永远非空，于是解密缓冲区被建成**线程局部全局**，汇编里只剩 `adrp x8, .L_v…` 的引用而没有任何定义；`.L` 前缀在 ELF 里代表「临时符号」，MC 层直接拒绝。
- **修法**：`tlsMode = GV->getThreadLocalMode();`（沿用它原本的 TLS 属性）；同时把 4 处继承 `GV->getLinkage()` 的全局变量改成 `GlobalValue::InternalLinkage`。
- **验证**：`ci/only-str.toml` 单 pass 编译 **0 错误**；用户真实配置（8 pass + `anti_debug=true`）出包成功，产物 19,595,824 B。

### A2 `relocation refers to a discarded section: .text.startup`

- **症状**：编译通过、链接阶段报 11 条 `.text.startup` 被丢弃。
- **排查**：先试 `--no-gc-sections` 无效（说明不是 `--gc-sections` 干的）；把 `FeatureElimination` 里「linkonce/weak → internal + 清 COMDAT」改成 `continue` 之后错误反而扩散到 `~vector`、`basic_string` 等内联函数 ⇒ 内联化那一步是必要的。
- **根因**：pass 把带 COMDAT 的函数拆段混淆，而 COMDAT 的语义是「组内要么全留要么全丢」，被拆分后链接器整组丢弃，其余引用变成指向废弃段。
- **修法**：`Utils.cpp` 的 `toObfuscate()` 里加一条硬跳过：
  ```cpp
  if (f->hasComdat() || f->hasLinkOnceODRLinkage() || f->hasWeakODRLinkage()
      || f->hasLinkOnceAnyLinkage() || f->hasWeakAnyLinkage()) return false;
  ```
  并在 `BASE` 里加 `-fno-asynchronous-unwind-tables -fno-unwind-tables`（CFI 段符号指向被丢段是同类问题的另一个入口）。
- **验证**：17 个编译单元全 `rc=0`，链接 `rc=0`，产物 85,642,896 B。

### A3 反调试把自己杀死了

- **症状**：带 `anti_debugging` 的产物**启动即静默退出**，无输出、无崩溃日志。
- **定位**：用 `nk_spawn`（ptrace 附加器）与普通启动对照；探针程序证明第一次 `PTRACE_TRACEME` 返回 0、第二次返回 `-1 EPERM`。而 pass 是「每个函数入口插一次 TRACEME」，第二次必然失败 ⇒ 被判定为「被调试」⇒ 走暴力退出。
- **修法**：`AntiDebugging.cpp` 不再插 `PTRACE_TRACEME`，改为在每个函数入口插 `call __nk_ad_check()`，判定逻辑全部移交运行时的 `ci/nk_ad.c`（它自己起看门狗进程，只认「外来 tracer」）。
- **验证**：`NK_AD_DEBUG=1` 日志 `TracerPid=0 wd=0 hooked=0`；普通启动正常打印自家界面；被 `nk_spawn` 附加时 `exit 97`。

---

## 阶段 B · 运行时看门狗 `nk_ad.c`（v1 → v11.1）

模块定位：**不参与混淆**的独立 C 文件（`clang -x c` 单独编成 `nk_ad_extra.o` 再链接），因为把检测代码本身丢进 pass 会触发 `Undefined temporary symbol` 且逻辑会被打散。

| 版本 | 新增能力 | 起因 |
|---|---|---|
| v1–v4 | `TracerPid` 探测、全线程扫描（`/proc/self/task`）、`maps` 差分、libc 序言扫描、自身 `.text` FNV、`prctl(PR_SET_DUMPABLE,0)`、直连 `svc #0`（openat=56 / read=63 / close=57 / write=64 / getdents64=61）、fail-open、命中 `_exit(97)` | 最初只查 `TracerPid`，被 Frida 与 ptrace 双混淆后失效 |
| v4 | 自家看门狗进程用 `/proc/<pid>/comm == "nk-wd"` 白名单放过；`maps` 差分从「新 r-x 段即杀」改成「只查 rwx 段」 | Vulkan/GL 驱动运行期 `dlopen` 合法，误杀 |
| v8 | `nk_maps_hit`（20 条特征：frida-agent / frida-gadget / libgum / gum-js-loop / frida-server / linjector / re.frida / lsposed / libxposed / xposedbridge / zygisk_lsposed / riru_ / edbg / stackplz / qbdi / libqbdi / unicorntrace / libunicorn / substrate / libhoudini_frida）、`nk_comm_hit`（逐 tid 扫 comm）、`nk_frida_socket`（`/proc/net/tcp{,6}` LISTEN 端口 27042 / 27043 / 27052，`/proc/net/unix` 找 `@frida`）、`nk_frida_files`、`nk_trace_state`、`nk_uprobe_brk`（扫自身 `.text` 前 8192 B 的 BRK 改写）、`nk_hwbp_hiccup` | 用户提供 204 行调试工具清单（Frida / LSPosed / eDBG / stackplz / QBDI / unicornTrace 等） |
| v10 | `nk_jni_table_ok`（JavaVM 函数表前 8 项必须落在 ART 模块内）、`nk_anon_exec`（memfd / 无路径可执行段）、`nk_svc_ok`（只含 `svc #0` 的 getpid 探针）、`nk_root_framework`、`nk_hwbp_slots`（`perf_event_open` + `HW_BREAKPOINT_X`）、`nk_tm_collect/verify`（主模块 + libc/libart/libc++/libm/libnativehelper/libdl 的 `.text` 哈希） | 第三方评审 `mj (3).md` 给出 8 条改进建议 |
| v10.2 | `g_text_rwx_self` + `NK_AD_NO_WLOCK` | `nk_text_wlock()` 自己把文本改成 RWX 后，下一轮「文本可写」判定把自己当攻击杀掉了 |
| v11 | memfd 放过 ART JIT（`/memfd:jit-cache`）、哈希名单砍到 主模块 + libc + libm + libdl、基线延后到首次深检、`errno → -fd`、`g_wlock_tried` 默认关、JNI 降为软分、maps 用 `%n` 解析、`/data/adb` 存在即算 root | `mj (4).md` 指出两个「真机必崩」P0：ART 的 JIT memfd 被误杀、ART 自己会 patch 自身 `.text` 导致哈希必然不符 |
| v11.1 | `jit` 改前缀精确匹配（防 `/memfd:frida-jit-agent` 绕过）、`g_jni_soft_done` 只记一次软分、`g_tm_ready` 原子三态、删掉 init 里冗余采样 | `mj (5).md` 指出：`strstr(q,"jit")` 可绕过、软分累加到 3 次即 `_exit(97)` 导致真机 4~6 秒自杀、`g_tm_ready` 非原子会让两线程同时写歪基线 |

**判定语义**：只有「外来 tracer / 命中特征」才走 `_exit(97)`；读失败一律 fail-open；`NK_AD_DEBUG=1` 时把每一步写进 `/data/local/tmp/nk_ad.log`。

---

## 阶段 C · CI 从 17.6 分钟压到 1.8 分钟

- `disk & deps` 从固定 `apt-get` 改成按需检查：**7.5 min → 0.02 min**。
- `smoke test` 默认 `skip_smoke=true`：**4 min → 0**。
- LLVM 预编译产物改走 Release `llvm-build-21.1.8`（`llvm-build.tar.zst` 232 MB），插件缓存按 `sha256(kit.tar.gz)[:12]` 命名自动失效，NDK 命中缓存。
- **结果：端到端 17.6 → 1.8 分钟**（同一天内实测）。

### C1 配额爆掉

- **症状**：run 19 秒失败，唯一报错 `##[error]Failed to CreateArtifact: Artifact storage quota has been hit.`，`build lucifer` 一步都没跑。
- **根因**：私有仓库 500 MB 配额，却堆了 **289 个 artifact / 24.4 GiB**（`llvm-build` 约 90 份 × 231 MB、`ndk-r29-cache` 786 MB）。
- **修法**：清理到 **5 个 / 16.8 MiB**；workflow 不再把 `llvm-build` / `ndk-r29-cache` 传成 artifact；二进制改由 **Release**（tag `nk-builds`）发布；artifact 统一 `retention-days: 3`。
- **副产品教训**：往已有 `retention-days: 30` 的 YAML 块里再插一个同名键会产生重复键，GitHub dispatch 直接回 **422 Unprocessable Entity**。

---

## 阶段 D · 大工程编译的三次事故（2026-11-22）

### D1 `build lucifer` 被 SIGTERM（exit 143）

- **症状**：17 文件工程在 `build lucifer` 第 161–182 秒被杀；日志只有 `##[error]Process completed with exit code 143.` 与 `The runner has received a shutdown signal`，一行编译错误都没有。
- **逐项排除**：不是 `timeout-minutes`（350）、不是 `concurrency`、不是 OOM 的 137、不是 `systemd-oomd`（关掉 + 加 16 GB swap 无效）。
- **根因**：把资源监控从 `> /tmp/nk_res.log` 改成 `| tee -a` 之后才看见 —— **`mem_avail` 全程只剩 119–398 MB**（16 GB 机器，swap 也满）。是内存耗尽导致的 job 终止。
- **修法三层**：
  1. 逐文件 `-c` 编译（原来是 17 个文件一次性交给一个 clang，出错无法定位，也无法按文件降级）；
  2. 大 TU 先试 `ci/nk-nofold.toml`（7 pass，跳过 `flattening`），没有再退 `nk-light.toml`（3 pass）；
  3. 大 TU 加 `-O1`。
- **判定口径**：用**预处理字节数**而不是源文件大小 —— `projects/jni/main.cpp` 只有 1.3 KB，但 `#include` 了 `pz.h`/`xz.h`/`Encrypt.h`，`-E -P | wc -c` 出来是 3,123,227 B。

### D2 `Illegal instruction`（SIGILL）

- **症状**：混淆后的产物一运行就 SIGILL。
- **定位**：按 `PT_LOAD|PF_X` 精确扫代码段 —— 新产物 35,519,648 B 里有 **10 处 `brk` + 8 处 `udf`**，对照老产物 0 处。反汇编上下文是 `ldr w8,[x23,#0x28f4]; cmp w8,w20; b.ne +4; brk #0xf000`，即扁平化分发器的状态校验。
- **根因**：`flattening` 把 switch 兜底标成 `unreachable`，`bcf` 的不透明谓词与 `split_blocks` 新增的块让这条「不可达」分支真的会被走到；LLVM 把 `llvm.trap/unreachable` 落成 `brk #0xf000`。
- **修法**：大 TU 跳过 `flattening`；`BASE` 加 `-fno-asynchronous-unwind-tables -fno-unwind-tables`。
- **验证**：新产物代码段 16,086,400 B，**`brk 0 / udf 0`**。

### D3 顺带修掉的编译/链接错误

| 报错 | 修法 |
|---|---|
| `use of undeclared identifier 'vkCreateAndroidSurfaceKHR'` | `BASE` 加 `-DVK_USE_PLATFORM_ANDROID_KHR`（注意要作为独立数组元素，塞进 `-D_FORTIFY_SOURCE=2` 那个字符串里会污染宏值） |
| `PxPreprocessor.h:454: Exactly one of NDEBUG and _DEBUG needs to be defined!` | `BASE` 加 `-DNDEBUG` |
| `clang++: error: unable to execute command: posix_spawn failed` | 编译用的 `CF` 数组过滤掉了 `--ld-path`，链接行要单独补 `"--ld-path=$LLD"` |
| `ld.lld: error: duplicate symbol: cJSON_*` | 提交源文件时剔除「被别的源文件 `#include` 的 `.c/.cpp`」 |
| `[3/3] 编译 …/nk_ad_extra.o` 把 `.o` 当源码 | `try()` 循环开头加 `case "$f" in *.o / *.a) objs+=("$f"); continue ;; esac` |

---

## 阶段 E · 桌面端 GUI（→ 发布版 v1.0）

- 从零重写为 `tools/NKBuild.py`（CustomTkinter，约 1000 行）：源码 / 工程·产物 / 反调试 / 开始 四张卡片 + 日志框 + 进度条 + ETA + 终止 + 打开产物。
- **关键修复**
  - 扫描分成「可编译单元」与「待上传文件」两份清单 —— 只传 `.cpp` 会导致 CI 报 `imgui.h file not found`。
  - blob 上传 4 次退避重试（实测 1 / 2 / 5 / 20 MB 全部 201，blob 单个上限 100 MB；`put /contents` 才有 1 MB 限制，只用于写 `ci/ui-<proj>.toml`）。
  - 扫描后剔除被 `#include` 的 `.c/.cpp`。
  - 输出目录多级回退：桌面 → 文档 → `~` → LOCALAPPDATA → TEMP（5 个候选，都建 `NKBuild-out\<产物名>`，全失败才报错；起因是受限令牌下 `PermissionError [WinError 5]`）。
  - 失败时列出失败的步骤名 + run 链接（原来只有 `!! 编译失败：failure`）。
- **1.6.0 加入卡密授权栏**：`tools/xlverify.py` 纯 Python 实现 SM4-CBC + HMAC-SM3 信封、设备码、登录/解绑；`start()` 首行校验，未授权不允许编译。

---

## 阶段 F · 手机端 GUI（开发期 v1.1 → v1.8.1，对外 v1.0）

| 版本 | 变化 |
|---|---|
| 1.1 | 首个可用版本：选目录、编译、下载产物 |
| 1.2 | 16 pass 分项勾选 + 本地生成 toml 上传；产物自动存 `Download/NKBuild` + 一键安装；ETA；底部液态玻璃导航（Haze） |
| 1.3 | 新增「预检」页：上传前扫危险文件（>70 MB 必崩 / >45 MB 很可能崩 / 文件名带空格或中文的编译单元必崩 / 图片字体文档只拖时间 / >60 编译单元易撞限流） |
| 1.4 | 换主人给的 5 张开源皮肤（每页一张，`Crossfade` 切页换图）、Q 版粉发女孩做应用图标与导航图标、浅色主题（`#F6EFF4` 底 / `#D2557F` 主色）；1.4.2–1.4.6 逐版调对比度、卡片不透明度、日志框、图标取景 |
| 1.4.1 | 切后台不丢任务：前台服务 `BuildService` 保活 + `runId` 断点续接 |
| 1.5.x | 自动跳过被 `#include` 的源文件；新增「任务历史」卡片（`工程·产物 + run id + 时间 + 是否已下载`，点一下重新下载） |
| 1.7 | 账号注册/登录、卡密充值、公告、云端版本检查（`api=ini` 的 `version`） |
| 1.8.0 | 4 个按钮音效（`SoundPool` 单流不叠 + 55 ms 节流）、震感分级、左上角自动消失提示条 |
| 1.8.1 | 按用户要求**只保留卡密**（去掉账号体系）；修「未激活不进充值页」（服务器对未激活账号回 `code=179`）；修「卡密已使用」（卡密一次性绑定，先 `unbind` 再登）；修到期时间（服务器把 `vip` 放在 `msg` 里且是字符串，`4102243200` 是秒）。这一版就是对外发布的 **v1.0** |

- **签名固定**：`android/keystore/nkbuild.p12`（PKCS12，自签 10,950 天），解决「CI 每次生成新 debug keystore，新包装不上旧包」的 `INSTALL_FAILED_UPDATE_INCOMPATIBLE`。
- **版本约定**：对外 `versionName` 固定 `1.0`，只递增 `versionCode`（当前 19）。

---

## 阶段 G · 改名与发布（2026-11-23）

- 全量改名（2026-11-23）：早期代号、仓库名、动态库名、脚本名、环境变量前缀统一换成 `NK` / `nk-obf` —— kit 目录 `nk/`（95 个文件）、产物 `libNk.so`、构建脚本 `ci/build-nk.sh`、环境变量 `NK` / `NK_CONFIG`，仓库仍为私有。
- 源码摊开进仓库 `src/`（79 文件 / 2.5 MB）、文档 8 篇、MIT LICENSE。
- 清仓库：删 `proj/`、`projects/myproj/`（37.4 MiB，光 `font.h` 就 20 MB）、`projects/路西法/`；**410 文件 / 63.3 MiB → 294 文件 / 24.9 MiB**。
- 发布 **Release `v1.0`**：`NKBuild-mobile-1.0-kami.apk`（9.98 MB）+ `NKBuild-1.0-win64.exe`（13.35 MB）；另有 `nk-builds`（每次构建的 `lucifer` 与 `nk-toolchain.tar.gz`）、`llvm-build-21.1.8`。
- 最新 CI：run `45041678195` success，`build lucifer` **41 秒**，产物 21,145,056 B，sha256 `6753A4C83F20A5FABEDC9903C1DD9741F23E5FA5E685A6534A20D14E5DF26F34`，代码段 16,086,400 B，`brk 0 / udf 0`，明文 `yz.dujiuyue.cn` 0 处、`kami` 0 处。
