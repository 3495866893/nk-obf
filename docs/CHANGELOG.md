# 更新记录

版本号语义：`v1.x` 期间只保证「同一份 TOML 在同一 LLVM 21 工具链上产物可复现」，跨 LLVM 大版本不保证 ABI。

| 版本 | 日期 | 摘要 |
| --- | --- | --- |
| v1.0 | 2026-11-03 | 初版：上游框架接入 NDK r29，核心 8 项 pass 可用 |
| v1.1 | 2026-11-06 | TLS 代码生成修复，字符串加密在 Android 上可稳定链接 |
| v1.2 | 2026-11-09 | COMDAT 修复，解决 `relocation refers to a discarded section` |
| v1.3 | 2026-11-12 | 大 TU 跳过 `flattening`，消除后端 OOM 与 `brk` 哨兵 SIGILL |
| v1.4 | 2026-11-15 | 反调试注入点外置为 `ci/nk_ad.c`，`nk_ad` 重写 |
| v1.5 | 2026-11-18 | 项目改名 NK，`nk_ad` 升到 v11.1 |
| v1.6 | 2026-11-20 | 桌面 GUI `tools/NKBuild.py` 与 CI 参数化 |
| v1.7 | 2026-11-22 | 手机版 Android App、卡密授权、CI 配额治理 |
| v1.8（当前） | 2026-11-23 | 文档体系重写，16 项 pass 状态如实标注 |

---

## v1.8 — 2026-11-23

**文档**

- 新增 `README.md` 与 `docs/` 全套六篇：FEATURES / COMPATIBILITY / USAGE / ARCHITECTURE / CREDITS / CHANGELOG。
- 逐项标注 pass 状态：核心 8 项默认开；`anti_hooking` / `anti_class_dump` / `objc` 默认关；`mba` / `chaos_state_machine` / `vector_obfuscation` / `function_wrapper` / `function_call_obfuscate` 实验性。
- 明示已知限制：大 TU（源 > 200 KB 或预处理 > 800 KB）跳过 `flattening`；`anti_dump = false` 会卡死。

**构建**

- `ci/*.toml` 预设重新整理，`tuned.toml` 作为核心 8 项全开的基准配置。
- `.gitignore` 覆盖 `build/ obj/ out/ .gradle/ __pycache__/ *.o *.a *.so *.exe *.apk *.tar.gz dist/`。

## v1.7 — 2026-11-22

**手机端**

- 新增 `android/` App：选目录 → 选 pass → 一键云端编译 → 产物落 `Download/NKBuild`，与桌面端共用同一套 workflow 输入。

**授权**

- 加入卡密授权校验，构建请求需带有效 token 才进入队列。
- 授权失败与编译失败分开计费口径，避免占用配额。

**CI**

- 制品保留期收紧，`cleanup.yml` 定期清理过期 artifact 与缓存。
- 并发任务上限下调，避免多端同时提交把 runner 打满。
- `rebuild_clang` 默认 `false`，仅 LLVM 变更时才强制重建工具链。

## v1.6 — 2026-11-20

**工具**

- 新增 Windows 桌面端 `tools/NKBuild.py`：GitHub token 认证、目录选择、pass 勾选、一键触发 `build-obf.yml`、产物下载到 `Download/NKBuild`。
- 编译日志在 GUI 内增量显示，失败时定位到具体 pass。

**CI**

- `build-obf.yml` 参数化：`src_dir` / `src_files` / `cfg_only` / `out_name` / `anti_debug` / `skip_smoke` / `rebuild_clang`。
- `cfg_only` 模式只产出配置不编译，用于快速验证 TOML 合法性。

## v1.5 — 2026-11-18

**改名**

- 项目由 Ensia 全面改名 **NK**：插件 `libNk.so`、配置键、CI 参数、GUI 标题、文档同步替换。
- 旧名仅保留在 `docs/CREDITS.md` 作为来源说明。

**`nk_ad` v11.1**

- 代码段哈希基线延后到首次深检采样，避开 ART 启动期自修改导致的误报。
- maps 差分放过 `/memfd:jit-cache` 等合法 JIT 映射；`libart` 不参与哈希。
- 新增硬件断点槽探测（`perf_event_open` + `HW_BREAKPOINT_X`，`ENOSPC` / `EBUSY` 判占）。
- 新增 `JavaVM` 前 8 项函数表校验。
- `text_wlock` 默认关闭，`NK_AD_WLOCK=1` 才开。

## v1.4 — 2026-11-15

**反调试**

- IR 层只保留 `call __nk_ad_check()` 注入点，实现移出到独立 C 模块 `ci/nk_ad.c`，编译为 `nk_ad_extra.o` 后链接。
- 反调试代码不再被 `bcf` / `flattening` 改写，行为稳定且可单独调试。
- 检查项：TracerPid 交叉校验、watchdog 子进程 `nk-wd` + `PTRACE_SEIZE` 自检、maps 差分、prologue 编码扫描、`svc #0` 直调 + `nk_svc_ok()` 通路校验、`prctl(PR_SET_DUMPABLE, 0)`、Frida / Xposed·LSPosed·Zygisk·Riru / eDBG·stackplz / QBDI·unicornTrace·substrate 指纹。
- 命中即 `_exit(97)`；`NK_AD_DEBUG=1` 输出到 stderr 与 `/data/local/tmp/nk_ad.log`。

## v1.3 — 2026-11-12

**大 TU 策略**

- 源文件 > 200 KB 或预处理后 > 800 KB 时自动跳过 `flattening`，其余 pass 照常。
- 修复由此引发的后端内存爆炸，以及异常路径产出 `brk` 哨兵导致的运行期 SIGILL。
- 阈值固定，不开放配置——它是后端的安全边界。

## v1.2 — 2026-11-09

**链接**

- 修复 COMDAT / `linkonce_odr` 函数被拆段后触发的 `relocation refers to a discarded section`。
- 改写前识别 COMDAT 成员并整函数跳过拆段与平坦化，保留 `comdat` 属性。

## v1.1 — 2026-11-06

**代码生成**

- 修复 `StringEncryption` 使用 TLS 模型导致 `.L_v` 临时符号未定义、Android 链接失败的问题。
- 生成端改回非 TLS 模型，按 `aarch64-linux-android` 的 TLS 描述符规则重写取址路径。
- 字符串加密在 NDK r29 + clang 21 上可稳定链接，`anti_dump = true` 路径验证通过。

## v1.0 — 2026-11-03

**初版**

- 上游混淆框架接入 LLVM 21 / NDK r29，产物 `libNk.so` 以 `-fpass-plugin` 加载。
- 核心 8 项 pass 可用：`string_encryption`、`constant_encryption`、`substitution`、`bcf`、`split_blocks`、`indirect_branch`、`flattening`、`anti_debugging`。
- TOML 配置模型成型：`[global]` 的 `preset` / `seed` / `verbose`，`[passes.<name>]` 的 `enabled`。
- `ci/build-lucifer.sh` 支持任意工程交叉编译到 `aarch64-linux-android21`。

---

## 配置键变更

| 版本 | 变更 | 迁移 |
| --- | --- | --- |
| v1.5 | 配置段由 `[ensia.*]` 改为 `[passes.*]` | 全文替换段名前缀 |
| v1.5 | 环境变量 `ENSIA=1` / `ENSIA_CONFIG` 改为 `NK=1` / `NK_CONFIG` | 替换构建脚本与 CI 变量 |
| v1.4 | 反调试不再是独立 pass 的 IR 生成 | 改用 `[passes.anti_debugging] enabled` + 编译期 `NK_AD=1` |
| v1.3 | `flattening` 增加 TU 规模判定 | 无需改配置，行为自动生效 |
| v1.0 | `constant_encryption.feistel` 永久关闭 | 配置里写 `feistel = true` 会被忽略 |
