# 贡献指南 · Contributing

先谢过。这个仓库最需要的是三类东西：**能复现的 bug 报告**、**真机兼容性数据**、**文档纠错**。

## 提 Issue 前

1. 先在 `docs/zh/COMPATIBILITY.md`（或 `docs/en/`）里确认你的环境是否已在支持列表内。
2. 跑一次最小验证：用 `projects/hello` 出包，确认流水线本身是通的。
3. 去 Actions 页看最近一次 `build-obf` 是不是绿的 —— 红的先看 run 日志，多半是配额或缓存问题。

## 提 Issue 时请带上

| 必填 | 说明 |
|---|---|
| 平台 | Windows 版本 / 手机型号 + Android 版本 + 是否 root（KernelSU / Magisk） |
| 版本 | Release 标签（如 `v1.0`）或 commit sha |
| 复现步骤 | 从「打开工具」开始写，写到出错那一步 |
| 日志 | 桌面 `%APPDATA%\NKBuild\startup.log`；手机 `Android/data/com.nk.nkbuild/files/crash/`；CI 直接贴 run 链接 |
| 预期 / 实际 | 一句话各写一个 |

## 提 PR 时

- 只改**一件事**，别把格式化混进功能改动。
- 改 `src/`（LLVM pass）请附一次 CI run 链接，证明 `build-obf` 还是绿的。
- 改 `tools/` 或 `android/` 请确保 `python -m py_compile` / `gradle assembleDebug` 通过。
- 新增文档请同时更新 `docs/zh/` 与 `docs/en/`，并在 `docs/*/CHANGELOG.md` 记一行。

## 授权

提交代码即表示你同意：`src/` `ci/` 的贡献按 **AGPL-3.0** 授权，`tools/` `android/` `docs/` 的贡献按 **MIT** 授权。
