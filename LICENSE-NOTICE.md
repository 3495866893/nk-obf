# 授权说明 · Licensing

这个仓库**不是单一授权**，按目录分成两块。`LICENSE` 文件放的是 AGPL-3.0 官方全文（适用于主体部分），
MIT 全文在 `LICENSE-MIT`。

| 路径 | 授权 | 为什么 |
|---|---|---|
| `src/` `ci/` `kit.tar.gz` | **AGPL-3.0** | 派生自 OLLVM-Next（Hikari 血脉），上游是 AGPL-3.0，必须继承；网络提供服务也要开源 |
| `tools/` `android/` `docs/` `projects/` | **MIT** | 本项目自研的双端 GUI、文档与示例 |

## 这意味着什么

- **自己用**：随便用，改了不用发。
- **二次分发 / 对外提供服务**：`src/`、`ci/`、`kit.tar.gz` 这部分（含你改过的插件）必须按 AGPL-3.0 开源；`tools/`、`android/` 这部分只要求保留 MIT 版权声明。
- **只改 GUI**：你改的 GUI 部分可以闭源，只要不动 `src/`。
- `kit.tar.gz` 里打包的是 `src/` 编出来的东西，所以跟着 AGPL-3.0 走。

## 上游

- OLLVM-Next —— 本项目的 pass 骨架来源，AGPL-3.0。
- Hikari —— 设计思路上的前辈（这一代 IR 方案定位为它的后继），不包含其代码。

完整致谢见 `docs/zh/CREDITS.md` 与 `docs/en/CREDITS.md`。
