# 架构

NK 由两块互不耦合的部分组成：**编译期的 IR 改写插件** `libNk.so`，与**运行期的自检模块** `nk_ad`（`ci/nk_ad.c` → `nk_ad_extra.o`）。前者只负责让二进制难读，后者只负责让运行环境不对。

---

## 分层视图

```
源码 (.c/.cpp)
   │  clang 21 前端
   ▼
LLVM IR
   │  ┌──────────────────────────── libNk.so ────────────────────────────┐
   │  │ PassPlugin 入口 → PassBuilder 回调 → 按固定顺序跑 16 个 module pass│
   │  └──────────────────────────────────────────────────────────────────┘
   ▼
改写后的 IR（含 call __nk_ad_check() 注入点、密文常量、平坦化状态机）
   │  LLVM 21 后端
   ▼
目标文件 (.o)  ──link──►  .so / .exe  ◄── nk_ad_extra.o（独立编译，不混淆）
```

关键设计：**反调试逻辑不写在 IR 里**。IR 只插一个外部函数调用，`nk_ad.c` 单独编译、单独链接、单独调试。这样反调试代码本身不会被 `flattening` / `bcf` 破坏，也不需要为它维护一份 IR 生成器。

## 插件生命周期

| 阶段 | 入口 | 行为 |
| --- | --- | --- |
| 加载 | `llvmGetPassPluginInfo` | 注册 PassBuilder 的优化管线回调 |
| 配置解析 | `[global]` / `[passes.*]` | 读 `NK_CONFIG` 指向的 TOML；缺失则用 `preset` |
| 总开关 | `NK=1` | 未设置时管线回调直接返回，不改写任何 IR |
| 模块 pass | `runOnModule` | 依次执行已启用的 pass，每个 pass 独立判定 `enabled` |
| 收尾 | `verifyModule` | 改写后跑一次 IR 验证，失败则丢弃改动并报错 |

配置优先级：`NK_CONFIG` 指定的 TOML > `preset` 内置值 > 编译期默认。环境变量在进程启动时读一次，同一进程内多次编译共用同一份配置。

## Pass 流水线顺序

顺序固定，配置只能决定某一项是否执行，不能调整先后。理由：数据加密要在控制流改写之前（否则解密调用自己被平坦化，运行期开销爆炸）；控制流改写要从轻到重；反调试注入放在最后，保证每个真实函数入口都有检查点。

| 序 | Pass | 阶段 | 放在此处的理由 |
| --- | --- | --- | --- |
| 1 | `string_encryption` | 数据 | 最早的字符串形态，便于全量收集 |
| 2 | `constant_encryption` | 数据 | 常量池建立后，后续 pass 才能复用 |
| 3 | `substitution` | 指令 | 单指令级改写，先做不影响块结构 |
| 4 | `mba` | 指令 | 同上，实验性项放在 `bcf` 前便于观察体积 |
| 5 | `vector_obfuscation` | 指令 | 需在控制流改写前保持向量形态 |
| 6 | `bcf` | 控制流 | 先建假分支 |
| 7 | `split_blocks` | 控制流 | 再切块，切完的块才进入平坦化 |
| 8 | `chaos_state_machine` | 控制流 | 优先尝试，块数超限自行跳过 |
| 9 | `flattening` | 控制流 | 兜底平坦化，也是大 TU 的跳过点 |
| 10 | `indirect_branch` | 控制流 | 平坦化后再把剩余跳转改间接 |
| 11 | `function_wrapper` | 结构 | 函数边界已定，做代理层 |
| 12 | `function_call_obfuscate` | 结构 | 调用点改写，最后做 |
| 13 | `anti_debugging` | 注入 | 每函数入口插 `__nk_ad_check()` |
| 14 | `anti_hooking` | 注入 | 完整性基线，默认关 |
| 15 | `anti_class_dump` | 注入 | ObjC/Java 类结构，默认关 |
| 16 | `objc` | 前端 | 仅 ObjC 工程 |

## 改写策略要点

| 手段 | IR 层做法 | 运行期表现 |
| --- | --- | --- |
| 字符串加密 | `ConstantDataArray` → 密文字节 + 解密调用 | 首次使用时解密，用后 `memset` 清零 |
| 常量加密 | 常量提全局 + XOR 分片重组 | 加载时算一次，结果留栈 |
| 平坦化 | 函数体压入 `switch` 状态机 + 分发器 | 每块一次间接跳转 |
| 虚假控制流 | 不透明谓词 + 永不执行块 | 谓词恒真，假块只是占位 |
| 间接跳转 | 跳转表进 `alloca` + 目标 XOR | 解密后 `br` 到寄存器 |
| 函数包装 | 原函数改名 + 生成 N 层代理 | 每层一次真实调用 |

## 工程约束与已修问题

这些问题决定了当前实现的形状，改动相关代码前需要知道：

| 问题 | 根因 | 当前做法 |
| --- | --- | --- |
| `.L_v` 临时符号未定义 | `StringEncryption` 生成的全局用了 TLS 模型，`thread_local` 临时符号在链接期被丢弃 | 生成端改为非 TLS 模型；Android 上按 `aarch64` 的 TLS 描述符规则重写 |
| `relocation refers to a discarded section` | COMDAT / `linkonce_odr` 函数被拆段后，原段被丢弃但重定位仍指向它 | 识别 COMDAT 成员，跳过拆段；`comdat` 属性整体保留 |
| 大 TU 后端 OOM + SIGILL | `flattening` 在超大函数上生成的状态机使后端内存爆炸，并在异常路径产出 `brk` 哨兵 | 源 > 200 KB 或预处理 > 800 KB 时跳过 `flattening`；其余 pass 照常 |
| 反调试逻辑被自身混淆 | 检查代码写在 IR 里，被 `bcf` / `flattening` 改写后行为不稳 | 逻辑移出到 `ci/nk_ad.c`，IR 只留调用点 |
| ART 启动期自修改导致基线误报 | ART 在启动时改自己的代码页 | 哈希基线延后到首次深检采样；`libart` 不参与哈希 |
| 合法 JIT 映射被当成注入 | `/memfd:jit-cache` 等映射无文件路径 | maps 差分显式放过 ART 的 JIT 映射 |

## 运行时反调试模块 `nk_ad`

`ci/nk_ad.c` 编译为 `nk_ad_extra.o`，与混淆产物一起链接。所有检查在 `__nk_ad_check()` 内按等级调度，命中即 `_exit(97)`（静默，不打印、不弹窗）。

| # | 检查项 | 实现要点 |
| --- | --- | --- |
| 1 | TracerPid 交叉校验 | 遍历 `/proc/self/task/*/TracerPid`，只认**外来** tracer（父进程合法调试除外） |
| 2 | watchdog 自检 | fork 子进程 `nk-wd`，用 `PTRACE_SEIZE` 试着 attach 父进程；成功即说明无人调试 |
| 3 | maps 差分 | 新增 `r-x` / `rwx` / `memfd` / 无路径 exec 映射即异常 |
| 4 | prologue 编码扫描 | 扫 libc/关键模块函数头，只认跳到模块外的 `B` / `BR` / `BLR` / `ldr+br` |
| 5 | 代码段 FNV 哈希基线 | 主模块 + `libc` / `libm` / `libdl`，延后到首次深检采样 |
| 6 | 直接系统调用 | `svc #0` 直调 `openat` / `read` / `close` / `write` / `getdents64`，绕开 libc |
| 7 | syscall 通路校验 | `nk_svc_ok()` 对直调结果与 libc 结果比对，识别 syscall hook |
| 8 | 不可 dump | `prctl(PR_SET_DUMPABLE, 0)` |
| 9 | Frida 指纹 | maps 名 / 线程名 / `27042`·`27043`·`27052` 端口 / `@frida` unix socket / 常见落地路径 |
| 10 | Root 框架指纹 | Xposed / LSPosed / Zygisk / Riru 目录；有 root 框架时只记软分，不直接退出 |
| 11 | uprobe 检测 | 扫自身 `.text` 找被改写的 `BRK` 指令（eDBG / stackplz 特征） |
| 12 | 框架指纹 | QBDI / unicornTrace / substrate |
| 13 | JNI/ART 校验 | `JavaVM` 前 8 项函数指针必须落在 ART 模块地址范围内 |
| 14 | 硬件断点槽 | `perf_event_open` + `HW_BREAKPOINT_X`；`ENOSPC` / `EBUSY` 判定槽位被占 |
| 15 | trace 状态 | `/proc/self/status` 的 `State:\tt`，或 `/proc/self/stat` 第 3 域为 `t` |
| 16 | 文本写保护 | `NK_AD_WLOCK=1` 时 `mprotect` 只读（默认关） |
| 17 | 日志 | `NK_AD_DEBUG=1` 时写 stderr 与 `/data/local/tmp/nk_ad.log` |

### 已知取舍

| 取舍 | 原因 |
| --- | --- |
| 放过 ART 的 `/memfd:jit-cache` | 合法 JIT 映射，误判率极高 |
| `libart` 不做哈希 | ART 会自改代码页，基线无法稳定 |
| `text_wlock` 默认关 | 部分 ROM / 加固壳与只读 `.text` 冲突 |
| root 框架只记软分 | 有 root 不等于被调试，直接退出误报太多 |
| 只认外来 tracer | 否则自身 watchdog 与合法调试器都会被判 |
| 静默 `_exit(97)` | 不给出攻击者任何反馈；调试时靠 `NK_AD_DEBUG` |

## 产物与构建面

| 产物 | 来源 | 用途 |
| --- | --- | --- |
| `libNk.so` | `ci/build-nk.sh` / `build-plugin.yml` | LLVM pass 插件 |
| `nk_ad_extra.o` | `ci/nk_ad.c` 单独编译 | 反调试运行时 |
| `libc++_static` / `libc++abi` / `libunwind` | NDK r29 sysroot | C++ 静态链接依赖 |
| 加固 `.so` | `ci/build-lucifer.sh` | 最终交付 |
| `Download/NKBuild` | 桌面 / 手机 GUI | 云端编译产物落盘 |

## 边界

| 不做的事 | 说明 |
| --- | --- |
| 加密算法本身 | 只搬运与包裹，不替换用户算法 |
| 反虚拟机 / 反沙箱 | `nk_ad` 不判定模拟器，只判定调试与注入 |
| 授权 / 卡密校验 | 属构建端工具链，不进混淆产物 |
| Java 层混淆 | 只处理原生代码；`anti_class_dump` 仅触及类结构 |
