# 功能清单：16 个 pass 与运行时检测项

> 本篇讲：16 个 pass 各自改写什么、默认开哪 8 个、哪些是不可选的实验项；以及 `ci/nk_ad.c` 在运行时到底查了哪些东西、哪些证据当场退出、哪些只记软分，最后附上传预检规则表。

## 总表

配置统一走 TOML：`[global]` 管全局，`[passes.<key>] enabled = …` 逐项开关。没写 `enabled` 的 pass 一律视为关闭。

| key | 中文名 | 作用 | 默认 |
| --- | --- | --- | --- |
| `string_encryption` | 字符串加密 | 字符串常量运行时解密，用完清零（`anti_dump`） | 核心 8 项，开 |
| `constant_encryption` | 常量加密 | 常量 globalize + XOR 分片，`feistel=false` | 开 |
| `substitution` | 指令替换 | 等价指令序列替换 | 开 |
| `bcf` | 虚假控制流 | 不透明谓词 + 假分支 | 开 |
| `split_blocks` | 基本块分割 | 每个函数切 3 段 | 开 |
| `indirect_branch` | 间接跳转 | 栈上跳转表 + 跳转目标加密 | 开 |
| `flattening` | 控制流平坦化 | 状态机 + 分发器 | 开（大 TU 自动跳过） |
| `anti_debugging` | 反调试（IR 级） | 每个函数入口插 `call __nk_ad_check()` | 开 |
| `mba` | MBA 混淆 | 位运算恒等式替换，体积大耗时长 | 关（实验） |
| `chaos_state_machine` | 混沌状态机 | 未充分验证 | 关（实验） |
| `vector_obfuscation` | 向量混淆 | 未充分验证 | 关（实验） |
| `function_wrapper` | 函数包装 | 未充分验证 | 关（实验） |
| `function_call_obfuscate` | 调用混淆 | 未充分验证 | 关（实验） |
| `anti_hooking` | 反 Hook | 完整性基线在本环境不稳定 | 关（不可选） |
| `anti_class_dump` | 反类转储 | 仅 ObjC/Java 场景 | 关（不可选） |
| `objc` | ObjC 混淆 | Android 无用 | 关（不可选） |

分组依据就是 GUI 里的三栏：**核心 8 项**（默认开、可选）、**进阶实验 5 项**（可选、默认关）、**不可用 3 项**（灰掉、不可勾选）。

## 核心 8 项

下面「默认值」取自手机端生成的 TOML（`PassCatalog.toml()`）与 `ci/nk-nofold.toml`，两者同构。

| pass | 关键参数 | 默认值 | 代价 |
| --- | --- | --- | --- |
| `string_encryption` | `probability` / `anti_dump` / `force_content` | `100` / `true` / `[".*"]` | 每串一次解密调用，首次解密进热路径 |
| `constant_encryption` | `iterations` / `share_count` / `globalize` | `2` / `6` / `true` | 常量池膨胀，`.data` 变大 |
| `substitution` | `probability` / `iterations` | `60` / `1` | 指令数 ×1.5–3 |
| `bcf` | `probability` / `complexity` / `junk_asm` | `60` / `6` / `false` | 代码量 ×2–4，分支预测变差 |
| `split_blocks` | `splits` / `stack_confusion` | `3` / `false` | 块数 +5–15，栈帧略增 |
| `indirect_branch` | `use_stack` / `enc_jump_target` | `true` / `true` | 跳转表进栈，间接跳转开销 |
| `flattening` | 无参数 | `enabled` 即全量 | 最重：编译内存 ×2–3，稳态 −10~−40% |
| `anti_debugging` | `probability` | `100` | 每函数一次外部调用（32 次调用才真正深检一次） |

几个必须记住的约束：

1. **`anti_dump = false` 会让程序卡在全局构造器**，实测组合已禁用。要关就整段 `enabled = false`。
2. **`constant_encryption.feistel` 永久关闭**：该路径在当前后端产出的 IR 过不了验证。
3. **`flattening` 在大 TU 上自动跳过**（源 > 200 KB 或预处理 > 800 KB），不需要在配置里写。
4. **`anti_debugging` 永远开着**：关掉 pass 会让产物在静态初始化阶段 100% CPU 死循环，所谓「关反调试」只是把 `nk_ad.c` 用 `-DNK_AD_OFF` 编成空实现。

### 核心 8 项的 TOML 骨架

```toml
[global]
preset = ""
seed = "0xDEADBEEF"
verbose = false

[passes.string_encryption]
enabled = true
probability = 100
anti_dump = true
force_content = [".*"]

[passes.constant_encryption]
enabled = true
iterations = 2
share_count = 6
feistel = false
globalize = true
globalize_prob = 100
substitute_xor = true
substitute_xor_prob = 100
skip_value = ["^0x0$"]

[passes.substitution]
enabled = true
probability = 60
iterations = 1

[passes.bcf]
enabled = true
probability = 60
iterations = 1
complexity = 6
entropy_chain = false
junk_asm = false
nested = false
create_func = false

[passes.split_blocks]
enabled = true
splits = 3
stack_confusion = false

[passes.indirect_branch]
enabled = true
use_stack = true
enc_jump_target = true

[passes.flattening]
enabled = true

[passes.anti_debugging]
enabled = true
probability = 100
```

## 进阶实验 5 项

| pass | 中文名 | 开了会怎样 | 建议 |
| --- | --- | --- | --- |
| `mba` | MBA 混淆 | 位运算恒等式展开，体积 +20–60%，编译时间明显上升 | 先试这一项，跑一次真机冒烟 |
| `chaos_state_machine` | 混沌状态机 | 双层分发，块数超 `max_blocks` 自行回落经典平坦化 | 大工程容易把编译内存吃光 |
| `vector_obfuscation` | 向量混淆 | 仅 `width = 128`（arm64 NEON），体积暴涨、收益低 | 一般不值得 |
| `function_wrapper` | 函数包装 | 每个函数加 `times` 层代理，可能破坏虚表 | 有 RTTI/虚表的重工程慎用 |
| `function_call_obfuscate` | 函数调用混淆 | Android 依赖 `dlopen`，崩溃后极难排查 | 出问题第一个回退它 |

开启顺序建议：`mba` → `vector_obfuscation` → `function_wrapper` → `function_call_obfuscate`，每开一项跑一次真机。回退顺序反过来。

## 不可用 3 项

| pass | 为什么灰掉 |
| --- | --- |
| `anti_hooking` | 完整性哈希基线在真机（ART 自修改 + 厂商 ROM）永远对不上，误报即退出 |
| `anti_class_dump` | 只对 ObjC / Java 类结构有意义，纯 C/C++ 工程零收益 |
| `objc` | 仅 ObjC 前端需要，Android 上没有作用 |

这三项在 GUI 里显示为不可勾选，手写 TOML 写了 `enabled = true` 也不会改变可用性判断——但建议别写。

## `nk_ad.c` 运行时检测清单

`ci/nk_ad.c` 是一个壳文件（8 条 `#include "nk_ad_parts/NN_*.inc"`，分块合计约 2,100 行），编成 `nk_ad_extra.o` 后链接进产物；它只过 `string_encryption` 一道 pass，入口符号每次构建随机。入口 `__nk_ad_check()` 的节奏是：**每 32 次调用才跑一次完整检查**（`(++g_calls & 31u) != 0` 直接返回），另有一个 detached worker 线程每 1.5 秒深检一次，其中「带 sleep / 故意缺页」的模拟器深度测试 4 秒一次。命中硬证据一律 `_exit(97)`，不打印、不弹窗。

### 一、反调试

| 项 | 实现 | 证据级别 |
| --- | --- | --- |
| TracerPid 交叉校验 | 读 `/proc/self/status` 的 `TracerPid`；`comm` 为 `nk-wd` 的自家看门狗进程放过 | 硬，外来 tracer 即退出 |
| 全线程扫描 | 遍历 `/proc/self/task/*/status`，任何一个线程被 trace 都算 | 硬 |
| watchdog 自检 | `fork` 出 `nk-wd`，用 `PTRACE_SEIZE` 试着 attach 父进程；能 attach 说明当前无人调试 | 结构性证据 |
| trace 状态 | `/proc/self/status` 的 `State: t`，或 `/proc/self/stat` 第 3 域为 `t` | 硬 |
| 直连系统调用 | 内联 `svc #0` 调 `openat=56` / `read=63` / `close=57` / `write=64` / `getdents64=61`，绕开 libc | 基础设施（同时也防 libc hook） |
| svc 自检 | `nk_svc_ok()`：只含 `svc #0` 的 `getpid` 探针与 libc `getpid()` 结果比对 | 硬，不一致即 syscall 被拦 |
| perf 硬件断点槽 | `nk_hwbp_slots()`：`perf_event_open` + `HW_BREAKPOINT_X` 申请执行断点，`ENOSPC`/`EBUSY` 说明槽位被别人占了 | 硬 |
| 不可 dump | `prctl(PR_SET_DUMPABLE, 0)`，切断 `ptrace` 与 `/proc/<pid>/mem` | 预防措施 |
| 文本写保护 | `NK_AD_WLOCK=1` 时 `mprotect` 自身 `.text` 为只读；自己先被改成 RWX 的场景由 `g_text_rwx_self` + `NK_AD_NO_WLOCK` 放过 | 可选，默认关 |
| 日志 | `NK_AD_DEBUG=1` 写 stderr 与 `/data/local/tmp/nk_ad.log` | 调试用 |

### 二、反注入

| 项 | 实现 | 证据级别 |
| --- | --- | --- |
| maps 黑名单 | `nk_maps_hit()` 扫 20 条模块名黑名单（`frida-agent` / `gadget` / `substrate` / `libzygisk` 等） | 硬 |
| maps 差分 | 启动时快照，后续新增的 `r-x` / `rwx` / 无路径 exec 映射参与判定；Vulkan/GL 驱动启动后 dlopen 的合法 `r-x` 放过 | 只对 `rwx` 硬杀，其余仅日志 |
| 匿名可执行段 | `nk_anon_exec()`：无文件路径的 exec 映射、`memfd:` 加载的 exec 段 | 硬 |
| Frida socket | `nk_frida_socket()`：`/proc/net/tcp{,6}` 找 `69A2`/`69A3`/`69AC`（即 27042/27043/27052）LISTEN，`/proc/net/unix` 找 `@frida`、`frida-server` | 硬 |
| Frida 文件 | `nk_frida_files()`：常见落地路径（`/data/local/tmp/frida-server` 等） | 硬 |
| 线程名特征 | `nk_comm_hit()`：逐个 tid 读 `comm`，命中注入器线程名黑名单 | 硬 |
| 进程名回读 | `nk_emu_name()`：写 `comm` 再读回，不一致说明被拦截 | 软分 |
| root 框架分级 | `nk_root_framework()`：Xposed / LSPosed / Zygisk / Riru 目录；`/data/adb` 存在即算 root。**有 root 不等于被调试**，只记软分 | 软分 |
| JNI 软分 | JNI 表异常只记一次软分（`g_jni_soft_done`），避免 1.5 秒周期里连记 3 次直接判死 | 软分，一次性 |

### 三、反 Hook

| 项 | 实现 | 证据级别 |
| --- | --- | --- |
| libc 序言编码扫描 | `nk_hooked_libc()`：扫 libc/关键模块函数头，只认跳到模块外的 `B` / `BR` / `BLR` / `ldr+br` 形态 | 硬 |
| 自身 `.text` 哈希 | 启动时 FNV 哈希主模块代码段，深检时重算比对 | 硬 |
| 多模块哈希 | `nk_tm_collect()` / `nk_tm_verify()`：只覆盖**主模块 + libc + libm + libdl**；基线**延后到首次深检**采样，避开 ART 启动期合法自修改 | 硬 |
| uprobe BRK | `nk_uprobe_brk()`：扫自身 `.text` 前 8192 B，找被改写成 `BRK` 的指令（eDBG / stackplz 特征） | 硬 |
| uprobe maps | `nk_uprobe_maps()`：maps 里出现可写 `.text` 或 `[uprobes]` 段 | 硬 |
| JNI 函数表 | `nk_jni_table_ok()`：`JavaVM` 函数表前 8 项必须落在 ART 模块地址范围内 | 软分（ART 会指向 JIT 区，硬杀误报太高） |
| syscall 通路 | `nk_svc_ok()`：直调与 libc 结果比对，识别 syscall hook（PLT hook 的最后一道） | 硬 |

### 四、反模拟器

`NK_AD_EMU=0` 可整体关闭这一组。设计上分「硬证据」与「软证据」两档，软证据累计 ≥ 3 分才退出（`g_emu_score >= 3`）。

| 项 | 硬证据 | 软证据 |
| --- | --- | --- |
| build.prop | `qemu` / `goldfish` / SDK 型号标记（`nk_emu_prop`） | — |
| 设备节点 | `/dev/qemu_pipe`、`/dev/goldfish_pipe` 等（`nk_emu_dev`） | — |
| CPU 信息 | `cpuinfo` 里 `implementer=0x00`（`nk_emu_cpu`） | — |
| auxv | 没有 vDSO、`platform`/`random` 为 0（`nk_emu_auxv`） | — |
| /proc 魔数 | `/proc` 不是 procfs（`nk_emu_procfs`） | — |
| uname | `uname` 与 `/proc/sys/kernel/osrelease` 不一致（`nk_emu_uname`） | — |
| 系统调用语义 | 无效调用应返回 `-ENOSYS` 却返回别的（`nk_emu_syscall`） | — |
| SIGSEGV 投递 | 访问已知不可读页，`si_addr` 必须对得上（`nk_emu_trap`） | 未投递 / 地址不对 |
| 计数器 | — | `cntfrq` 异常、`cntvct` 与墙钟比例 < 0.25 或 > 4.0（`nk_emu_timer`） |
| 进程名 / 线程 | — | `comm` 回读失败、`/proc/self/task` 只见 1 个线程（`nk_emu_name` / `nk_emu_task`） |
| maps 形态 | — | 缺 `[stack]` 或 `[vdso]`（`nk_emu_maps`） |

### 环境变量总控

| 变量 | 阶段 | 作用 |
| --- | --- | --- |
| `NK_AD=1` | 编译期 | 链接真实 `nk_ad_extra.o` |
| `NK_AD=0` | 编译期 | `-DNK_AD_OFF` 编成空实现（pass 仍开） |
| `NK_AD_DEBUG=1` | 运行期 | stderr + `/data/local/tmp/nk_ad.log` 输出每一项判定 |
| `NK_AD_WLOCK=1` | 运行期 | 打开 `.text` 只读保护 |
| `NK_AD_NO_WLOCK=1` | 运行期 | 放过「自己把 text 改成 RWX」的场景 |
| `NK_AD_EMU=0` | 运行期 | 关闭反模拟器整组检查 |

真机日志格式如下，排查误报就看这一行：

```
nk_ad[chk] early=0 wd=12345
nk_ad[chk] TracerPid=0 wd=12345
nk_ad[chk] hooked=0
nk_ad[chk] text 8f3a... vs 8f3a...
```

`TracerPid=0 wd=0 hooked=0` 就是「无反调试误报」的标准形态。

## 上传预检规则

手机端在上传前扫一遍文件，把「一定崩」和「大概率崩」的区别讲清楚。GitHub 的硬限制是 `git/blob` 单文件 100 MB、`contents` 接口 1 MB（只用来传 toml）。

| 判定 | 条件 | 级别 | 说明 |
| --- | --- | --- | --- |
| 单文件 > 70 MB | 任意文件 | BLOCK | base64 后约 93 MB，请求体过大，直接跳过 |
| 编译单元名带空格 | `.c/.cpp/...` 且文件名含空格 | BLOCK | CI 用 `SRC_FILES="a.cpp b.cpp"` 按空格切，会被切成两截 |
| 编译单元名非 ASCII | 文件名有中文/特殊字符 | BLOCK | GitHub 能存，CI 侧 shell 处理容易出错 |
| 单文件 > 45 MB | 任意文件 | HIGH | base64 后约 60 MB，上传很可能超时 |
| 大体积非源码 | > 1 MB 且扩展名在 junk 表 | HIGH | png/ttf/psd/fbx… 编译用不上，白等上传 |
| 编译单元 > 60 个 | 整个工程 | HIGH | 一次提交文件太多，容易撞 GitHub 限流 |
| 总大小 > 120 MB | 整个工程 | HIGH | CI 拉取与上传都很慢 |
| 单文件 > 1 MB | 源码 | NOTE | 走 blob 接口，能传但慢 |
| 非源码扩展名 | 任意大小 | NOTE | 会被一起上传 |

junk 扩展名表覆盖：图片（`png jpg jpeg webp gif bmp ico svg`）、字体（`ttf otf woff woff2 ttc`）、音视频（`mp4 mp3 wav avi mkv mov`）、文档（`pdf doc docx xls xlsx ppt pptx`）、设计/模型（`psd ai blend fbx obj glb gltf`）、压缩包与二进制（`7z rar tar gz xz iso bin pyc pdb ilk exp lib dylib`）。

## 强度与代价

| 指标 | 核心 8 项 | 全开 |
| --- | --- | --- |
| 代码体积 | ×2–3 | ×5–10 |
| 编译时间 | ×1.5–2 | ×2–4 |
| 启动耗时 | +5–15% | +20–60% |
| 稳态性能 | −10~−40%（`flattening` 主导） | −30~−60% |

混淆强度 ≠ 绝对安全。这些手段抬高的是「读明白」和「调起来」的成本：`strings` 拿不到明文、IDA 里是一坨状态机、Frida 一 attach 就 `exit 97`；但算法仍在二进制里，只是需要更多时间和耐心。

## 实验项（16 个 pass 全部开放）

以前双端把 8 个 pass 标成「实验 / 禁用」，现在它们都能勾了 —— 依据是 CI 单 pass 实跑：

| pass | 作用 | CI 结论（`projects/hello` 单 pass） | 风险提示 |
|---|---|---|---|
| `mba` | 位运算恒等式替换 | ✅ 编过，cfg 1,796,344 B | 体积和编译时间都涨 |
| `chaos_state_machine` | 更激进的状态机 | ✅ 编过，1,801,576 B | 可能改坏逻辑，先小工程试 |
| `vector_obfuscation` | 向量化混淆 | ✅ 编过，1,802,232 B | 体积涨得多、收益一般 |
| `function_wrapper` | 函数包装 | ✅ 编过，1,799,832 B | 动调用约定，虚表场景慎用 |
| `function_call_obfuscate` | 调用点混淆 | ✅ 编过，1,804,888 B | 崩了不好排查 |
| `anti_hooking` | 内联 aarch64 完整性校验 | ✅ 编过，1,792,456 B | 会改函数体，可能连带崩，建议单独试 |
| `anti_class_dump` | 类 dump 防护 | ✅ 编过，1,791,128 B | 原本给 Objective-C，AArch64 上基本空转 |
| `objc` | ObjC 混淆 | ✅ 编过，1,797,528 B | 纯 C/C++ 工程里是空操作 |

用法：桌面端「② 混淆强度 → 混淆选项…」里逐项勾；手机端「混淆」页三个分组里逐项点开，「满血」预设现在就是 **16 个全开**。
建议顺序：先标准 8 个跑通 → 再单个加实验项 → 出问题就回退到标准。
