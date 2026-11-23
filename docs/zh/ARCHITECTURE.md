# 架构：从源码目录到加固产物

> 本篇讲：五段流水线怎么串起来、16 个 pass 在 IR 上的固定执行顺序、大 TU 的三级降级为什么长这样、`nk_ad.c` 的自混淆边界、CI 缓存与 Release 怎么分工、断点续接与任务历史用什么数据结构。

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

## `nk_ad.c` 的自混淆边界

`ci/nk_ad.c` 单独编译成 `nk_ad_extra.o` 再进链接列表，**不是「完全不混淆」，而是只过一道 `string_encryption`**（`ci/nk-self.toml`）。边界是这么定的：

1. **字符串必须加密**。加固前成品里 `nk_ad` 明文 79 处、`TracerPid` 2 处、`frida` 23 处，`strings` 一跑就把检测清单读完了。现在只上 `string_encryption`，这几个计数降到 1 / 0 / 0（剩下的 1 处是 `/data/local/tmp/nk_ad.log` 日志路径）。
2. **语义类 pass 一个不能上**。`bcf` / `indirect_branch` / `split_blocks` / `flattening` / `constant_encryption` / `substitution` 全部叠上去，实测产物在**全局构造函数里 `usleep` 死循环**（`debuggerd` 栈：`nanosleep ← usleep ← call_constructors`，`State: S`、`wchan=__arm64_sys_nanosleep`、日志 0 行）。另外单独编译时若把 `nk_ad.c` 丢进 `SRCS` 一起过 pass，会出 `Undefined temporary symbol .L_vXXXX` 汇编错误。
3. **入口符号每次构建随机**。`build-lucifer.sh` 生成 `NK_AD_SYM="__nk_$(head -c8 /dev/urandom | od -An -tx1 | tr -d ' \n')_g"` 并 export，插件侧用 `getenv("NK_AD_SYM")` 取名字 `getOrInsertFunction`，运行时侧用 `#ifndef NK_AD_SYM / #define NK_AD_SYM __nk_ad_check` 兜默认值 —— 成品里搜不到固定符号名。
4. **`NK_SKIP_ADB=1` 防自指**。给 `nk_ad.c` 自己过 pass 时，`anti_debugging` 会往它的每个函数入口再插一次 `call <NK_AD_SYM>()`，递归且无意义；插件在 `runOnFunction` 开头读这个变量直接返回。
5. **必须留名的符号**。IR 里内联的 ART 表校验要链接到 `__nk_art_lo` / `__nk_art_hi` / `__nk_vm` / `__nk_ad_panic`；P-3 编译期基线回填要写进 `__nk_base_hash` / `__nk_base_exempt`。这 6 个名字是设计上的例外，其余全部随机或加密。
6. **可整体关闭**。`NK_AD=0` 时用 `-DNK_AD_OFF` 编成空实现，`nk_die_r()` 与 `NK_AD_SYM()` 两处提前返回，文件其余部分保留（注入点仍然有定义）。

```bash
# 反调试模块：单独编译，只过 string_encryption（跳过 flattening 防炸内存）
NK_AD_SYM="__nk_$(head -c8 /dev/urandom | od -An -tx1 | tr -d ' \n')_g"
NK_SKIP_ADB=1 NK=1 NK_CONFIG="$PWD/nk/config/presets/nk-self.toml" \
"$CLANG" -x c --target=aarch64-linux-android21 --sysroot="$SYS" \
  "-resource-dir=$TC/lib/clang/21" -O2 -fPIE -fno-ident -fvisibility=hidden \
  "-DNK_AD_SYM=$NK_AD_SYM" -fpass-plugin="$NK_PLUG" \
  -c "$PWD/ci/nk_ad.c" -o "$PWD/nk_ad_extra.o"
```

**为什么不能干脆关掉 `anti_debugging` pass**：关掉之后注入点没了，但实测产物在静态初始化阶段 100% CPU 死循环、输出 0 字节。所以 pass 永远开着，「关闭反调试」只换实现。

### v12 加固（P/S 系列）

v12 的取舍原则：**能软分的绝不判死，判死的地方不留唯一出口**。六项 P 系列改的是判据的结构与处置路径，S 系列针对模拟执行。

| 编号 | 问题 | 改法 | 档位 |
| --- | --- | --- | --- |
| P-1 | 30+ 个检测点全汇到 `nk_die_r()` 的一句 `_exit(NK_EXIT)`，改一条 `bl` 就全线失效 | `nk_panic_ill()`（`brk #0x1` → SIGILL）/ `nk_panic_spin()`（伪装 ANR，忙等 5 秒后 exit，自带超时）/ `nk_panic_exit()`（裸 `svc #0` exit_group），`nk_fatal_poly()` 按 `g_panic_n % 3` 轮换 | 机制（消除唯一处置点） |
| P-2 | 看门狗 pid 被回收复用后可冒充 | `nk_wd_reap()` 回收僵尸 + `kill(pid,0)` 探活；`nk_peer_verify()` 用 `/proc/<pid>/stat` 第 22 域 starttime 校验身份 | 软分（避免误杀加固壳 / 厂商性能守护） |
| P-3 | 自哈希只覆盖 `.text` 前 1 MB | `nk_seg_verify()` 切 8 段轮转，1.5 秒一段、12 秒覆盖全量，单轮成本 1/8；首轮只采样不比对 | 硬证据 |
| P-4 | 判据靠枚举已知字符串 | `nk_struct_scan()` 按结构判定可执行映射的合法性；`nk_thread_anomaly()` 改成「总数 + 白名单外计数 > 12」 | 软分 |
| P-5 | 只伪造 `/proc` 一条通道就成立 | `nk_cross_conflict()` 比对裸 `svc #0` 的 `getppid`(173) 与 `/proc/self/status` 的 `PPid` | 软分 |
| P-6 | 5 处各自 `malloc(256 KB)` 全扫 maps | `nk_vma_t` + `nk_vma_refresh()`：1 MB 单次读、`sscanf("%lx-%lx %7s %*s %*s %*s %n")` 解析，`g_vma_gen` 保证同轮幂等 | 机制（单一数据源） |
| S-1 | 微架构判据直接判死 | 只翻 `g_uarch_flags` 位 | 软分 |
| S-2 | 写死 24 MHz / 19.2 MHz 跨不了 SoC | 构造函数里 `nk_cntvct_calibrate()` 用两个短窗口标定 `cntvct` / `CLOCK_MONOTONIC` 比例，`nk_uarch_update()` 只测漂移，相对偏差 > 35% 置位 | 软分 |
| S-4 | 模拟器的 syscall 分发表伪装 `ENOSYS` | `nk_syscall_semantic()` 探 0x7fff / 0x0e00 / 0x3a00，返回既非 `-ENOSYS`(-38) 也非 `-EPERM`(-1) 记分 | 软分 |
| S-6 | 缓存 fd 会自占槽位 | 逐槽 `perf_event_open` 后立即 close，统计可用槽数（< 3 记分），绝不缓存 fd；仅在检出 root 框架时执行 | 软分 |

### v12.2 补充（E / N / Q 系列）

| 编号 | 改法 | 档位 |
| --- | --- | --- |
| E-2 | `nk_wd_reseize()` 反向 `PTRACE_SEIZE` 看门狗：成功→`INTERRUPT`+`wait4(WNOHANG)`+`DETACH`；`EPERM`→`wd-seized-by-other`；`ESRCH`→`wd-gone` | 软分 |
| E-4 | `nk_uarch_consume()` 给 `g_uarch_flags` 接消费点：漂移 + `g_emu_score>=2` 才硬处置 | 合判 |
| E-6 | `nk_prop_get()`（`__system_property_get`）读 `ro.kernel.qemu` / `ro.hardware` / `ro.product.cpu.abi` 当第二信源 | 软分 |
| N-1 / N-2 | `prctl(PR_GET_SECCOMP)` 与 `getauxval(AT_HWCAP[2])` 只写日志，不打分 | 仅日志 |
| N-3 | `readlink("/proc/self/exe")` 校验安装域，域外记 `self-path-outside-install` | 软分 |
| Q-3 | 启动 8 秒内 `g_wd_pid<=0` 且 comm 为 `nk-wd` 时放过 | 防误杀 |
| Q-6 | `g_vma_trunc`：maps 读满/条目打满时不据结构判硬 | 防误杀 |

新增分块 `ci/nk_ad_parts/09_nk_env_probe.inc`，壳里在 `08` 之前 include（`09` 只依赖前面已定义的工具函数）。

P-4 用的两组常量：路径白名单前缀 `/system/`、`/system_ext/`、`/vendor/`、`/apex/`、`/product/`、`/odm/`、`/data/app/`、`/data/data/`、`/data/user/`、`/data/dalvik-cache/`、`/data/local/`、`/mnt/`、`[`；ART JIT 的 `/memfd:` 前缀是 `jit-cache`、`jit-zygote`、`jit_zygote`、`jit_cache`，命中即放行。

接线：`nk_tool_scan(!early)` 依次跑 `nk_seg_verify → nk_struct_scan → nk_peer_verify → nk_cross_conflict → nk_uarch_update → nk_syscall_semantic → nk_behavior_scan`；低频的 `nk_thread_anomaly` / `nk_hwbp_slot_count` 放进 60 秒一次的行为检查；看门狗线程每轮跑 `nk_wd_reap + nk_seg_verify`。

v12 之后又加了两层：

- **P-3 编译期基线**：链接时先不 strip，跑 `ci/tools/nk_patch_baseline.py` 把主代码段按 64 段逐段 FNV-1a 写进 `__nk_base_hash[]`，命中重定位目标的段在 `__nk_base_exempt[]` 里置位；再 `brand`、最后 `llvm-strip --strip-all`。运行时 `nk_cbase_check()` 每轮只比对一段，12 秒覆盖全量，改任何一段都会撞 `nk_ad: text chunk differs from build baseline`。
- **注入位置随机化**：插件把「函数名 FNV-1a + `NK_AD_SEED`」混成 `nkH`，`nkH % 100 >= 62` 的函数不插（约 38% 跳过），插入点在入口/第 2/第 3 个基本块之间轮换，ART 表校验只在一半函数展开 —— 成品里找不到整齐划一的注入模式。

文件本身也从 2,099 行单文件拆成了壳 + 8 个分块：`ci/nk_ad.c` 只剩头文件与 `#include "nk_ad_parts/NN_*.inc"`，分块按原始顺序编号，`include` 顺序 = 原文件顺序，token 流等价（这样拆分不引入任何新错误）。

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
| `NKBuild-1.0.2-win64.exe` | PyInstaller onefile | Release v1.0.2 |
| `NKBuild-mobile-1.0.2-kami.apk` | `build-apk.yml`（`assembleDebug` + 固定签名） | Release v1.0.2 |

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
