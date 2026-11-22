# 功能清单（16 个 Pass）

本文逐个说明 NK 的 16 个 LLVM pass：它改写什么 IR、暴露哪些参数、默认是否开启、代价多少。

所有 pass 都通过同一个 TOML 配置开关：

```toml
[passes.<name>]
enabled = true
```

`enabled` 缺省即不参与流水线；写了 `[passes.<name>]` 段但 `enabled = false` 等价于关闭。

---

## 总览

| # | Pass | 类别 | 默认 | 主要代价 |
| --- | --- | --- | --- | --- |
| 1 | `string_encryption` | 数据 | 开 | 每串一次解密调用，体积 +3–8% |
| 2 | `constant_encryption` | 数据 | 开 | 常量池膨胀，`.data` 变大 |
| 3 | `substitution` | 指令 | 开 | 指令数 ×1.5–3 |
| 4 | `bcf` | 控制流 | 开 | 代码量 ×2–4，分支预测变差 |
| 5 | `split_blocks` | 控制流 | 开 | 块数 +5–15，栈帧略增 |
| 6 | `indirect_branch` | 控制流 | 开 | 跳转表进栈，间接跳转开销 |
| 7 | `flattening` | 控制流 | 开（大 TU 跳过） | 最重：编译内存 ×2–3，运行 -10–40% |
| 8 | `anti_debugging` | 运行时 | 开 | 每函数一次外部调用 |
| 9 | `anti_hooking` | 运行时 | **关** | 完整性基线在真机易误报 |
| 10 | `mba` | 指令 | 实验性 | 体积 +20–60% |
| 11 | `chaos_state_machine` | 控制流 | 未充分验证 | 分发器嵌套，块数上限敏感 |
| 12 | `vector_obfuscation` | 指令 | 实验性 | 仅 `width = 128`（arm64 NEON） |
| 13 | `function_wrapper` | 结构 | 实验性 | 每函数 `times` 层代理 |
| 14 | `function_call_obfuscate` | 结构 | 实验性 | Android 依赖 `dlopen`，最易炸 |
| 15 | `anti_class_dump` | 运行时 | **关** | 仅对 ObjC / Java 类结构有意义 |
| 16 | `objc` | 前端 | **关** | 非 ObjC 工程无作用 |

---

## 核心 8 项

### 1. `string_encryption` — 字符串常量加密

把 IR 里的字符串常量替换为密文 + 运行期解密调用，解密后使用、用完清零。

| 参数 | 取值 | 说明 |
| --- | --- | --- |
| `enabled` | `bool` | 总开关 |
| `probability` | `0–100` | 按百分比抽样加密，60 是常用值 |
| `anti_dump` | `bool` | `true` 时更彻底：密文不驻留可读区，解密结果不留副本 |
| `force_content` | `string[]`（正则） | 强制加密的匹配集，`[".*"]` 表示全量 |
| `skip_content` | `string[]`（正则） | 排除集，典型 `["^%[0-9]*[a-zA-Z]$"]` 放过 printf 格式串 |

⚠️ `anti_dump = false` 已知会导致程序卡死，不要用。需要关闭该 pass 时整段 `enabled = false`。

### 2. `constant_encryption` — 常量加密

整数常量全局化进常量池，再按分片做 XOR 拆分，运行时重组。

| 参数 | 取值 | 说明 |
| --- | --- | --- |
| `iterations` | `1–3` | 拆分轮数，2 为均衡值 |
| `share_count` | `2–8` | 每常量分片数，8 时体积最大 |
| `globalize` | `bool` | 常量提到全局池，跨函数复用 |
| `globalize_prob` | `0–100` | 全局化概率 |
| `substitute_xor` | `bool` | 用 XOR 组合替代直接赋值 |
| `substitute_xor_prob` | `0–100` | 替换概率 |
| `skip_value` | `string[]`（正则） | 跳过的值，典型 `["^0x0$"]` |
| `feistel` | — | **永久关闭**：该路径在当前后端产出的 IR 无法稳定通过验证 |

### 3. `substitution` — 等价指令替换

把算术/位运算替换为等价但更长的指令序列。

| 参数 | 取值 | 说明 |
| --- | --- | --- |
| `probability` | `0–100` | 抽样率 |
| `iterations` | `1–3` | 叠加层数，2 时指令数约 ×2 |

### 4. `bcf` — 虚假控制流

插入不透明谓词与永不成立（但对分析器不可判）的假分支，并在假路径填充垃圾指令。

| 参数 | 取值 | 说明 |
| --- | --- | --- |
| `probability` | `0–100` | 函数抽样率 |
| `iterations` | `1–3` | 叠加轮数 |
| `complexity` | `1–10` | 谓词嵌套复杂度，10 为上限 |
| `entropy_chain` | `bool` | 谓词串联成熵链，单项误判即整链走假路径 |
| `junk_asm` | `bool` | 假路径填内联汇编垃圾 |
| `junk_asm_min` / `junk_asm_max` | `1–32` | 垃圾指令条数区间，常用 4 / 10 |
| `nested` | `bool` | 允许嵌套生成 |
| `create_func` | `bool` | 把假路径抽成独立函数 |
| `only_junk_asm` | `bool` | 只做垃圾填充，不建假分支 |

### 5. `split_blocks` — 基本块切分

把长基本块切成多段，段间插入跳转，配合 `stack_confusion` 打乱栈上布局。

| 参数 | 取值 | 说明 |
| --- | --- | --- |
| `splits` | `1–16` | 单块最多切几刀，10 是常用上限 |
| `stack_confusion` | `bool` | 关键局部变量改走栈上间接槽 |

### 6. `indirect_branch` — 间接跳转

跳转表放到栈上，跳转目标加密存储，运行时解密后再跳。

| 参数 | 取值 | 说明 |
| --- | --- | --- |
| `use_stack` | `bool` | 跳转表上栈（否则留在 `.rodata`） |
| `enc_jump_target` | `bool` | 目标地址 XOR 加密 |

### 7. `flattening` — 控制流平坦化

把函数体压成一个 `while(1) + switch` 状态机，真实块由状态变量分发。

- 无额外参数，`enabled` 即全量。
- **大 TU 自动跳过**：源文件 > 200 KB 或预处理后 > 800 KB 时不执行本 pass——否则后端内存爆炸并生成 `brk` 哨兵，运行期 SIGILL。
- 与 `chaos_state_machine` 同时开启时，后者块数超限会回落到经典平坦化。

### 8. `anti_debugging` — 反调试注入点

在每个函数入口插入 `call __nk_ad_check()`，实现细节全部在 `ci/nk_ad.c`，IR 层只留调用点。

| 参数 | 取值 | 说明 |
| --- | --- | --- |
| `probability` | `0–100` | 函数抽样率，60 时约六成函数带检查 |

编译期需定义 `NK_AD` 才会链接 `nk_ad_extra.o`；未链接时调用点为空实现。

---

## 实验性 5 项

| Pass | 参数 | 现状 |
| --- | --- | --- |
| `mba` | `probability` / `layers`（1–3）/ `heuristic` | 位运算恒等式展开，体积大，**实验性** |
| `chaos_state_machine` | `warmup`（默认 256）/ `nested_dispatch` / `max_blocks`（默认 10000） | 双层派发，块数超限跳过，**未充分验证** |
| `vector_obfuscation` | `probability` / `width`（仅 128）/ `shuffle` / `lift_comparisons` | 向量化改写，**实验性** |
| `function_wrapper` | `probability` / `times`（1–3） | 每函数生成 `times` 层代理调用 |
| `function_call_obfuscate` | `flag` / `symbol_config_path` | Android 端依赖 `dlopen`，崩溃高发项 |

这五项在默认预设中全部关闭。开启顺序建议：先 `mba`，再 `vector_obfuscation`，最后才考虑 `function_call_obfuscate`；每开一项跑一次真机冒烟。

---

## 默认关闭 3 项

| Pass | 参数 | 关闭原因 |
| --- | --- | --- |
| `anti_hooking` | `inline_x86` / `inline_aarch64` / `inline_win` / `objc_runtime` / `antirebind` / `direct_syscall` / `check_integrity` | 完整性哈希基线在真机（ART 自修改、厂商 ROM）易误报，误报即退出 |
| `anti_class_dump` | `dummy_count`（默认 8） | 只对 ObjC / Java 类结构有效，纯 C/C++ 工程无收益 |
| `objc` | — | 仅 ObjC 前端需要，其他工程开启了也无作用 |

需要更强抗注入时再单独打开 `anti_hooking`，并在目标机型上验证误报率。

---

## 组合建议

| 场景 | Pass 组合 | 说明 |
| --- | --- | --- |
| 快速验证链路 | `string_encryption` + `substitution` | 编译最快，体积 +5% 内 |
| 常规发布 | 核心 8 项（见 USAGE 示例 TOML） | 体积约 ×2–3，编译 ×1.5–2 |
| 强对抗 | 核心 8 项 + `mba` + `vector_obfuscation` | 体积 ×3–5，需真机回归 |
| 全开 | 再加 `function_wrapper` / `function_call_obfuscate` / `anti_hooking` | 体积 5–10×，编译 2–4×，闪退优先按此顺序回退 |

## 代价参考（实测区间，`-O2`，arm64）

| 指标 | 核心 8 项 | 全开 |
| --- | --- | --- |
| 代码体积 | ×2–3 | ×5–10 |
| 编译时间 | ×1.5–2 | ×2–4 |
| 启动耗时 | +5–15% | +20–60% |
| 稳态性能 | -10–40%（`flattening` 主导） | -30–60% |

混淆强度 ≠ 绝对安全：这些手段提高的是静态分析与逆向的时间成本。
