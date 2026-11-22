# 部署到自己的仓库（保姆级）

> 目标：把 NK 跑在**你自己的 GitHub 仓库**里，手机端和电脑端都能一键出包。
> 全程只用浏览器 + 两个 GUI，不需要在本机装 clang / NDK / Python。
> 预计时间：第一次 15 分钟（其中 CI 自建 clang 约 40–100 分钟，只需一次）。

---

## 0. 你会得到什么

```
你的 GitHub 仓库
├─ .github/workflows/build-obf.yml   ← 出混淆后二进制（Release nk-builds）
├─ .github/workflows/build-apk.yml   ← 出自己的手机端 APK
├─ ci/       构建脚本、配置（含 nk_ad.c 运行时看门狗）
├─ src/      插件源码（LLVM 21 Pass）
├─ kit.tar.gz 预编译插件包（CI 直接用它，省 40 分钟）
└─ tools/ android/  双端 GUI 源码
```

打包链路：`GUI 上传源码 → 触发 workflow → CI 里 clang 逐文件编译（挂 libNk.so） → 链接 aarch64 → 产物进 Release`。

---

## 1. 准备仓库（二选一）

### 方案 A：直接 fork 官方仓库（最快）

1. 打开 <https://github.com/3495866893/nk-obf>，右上角 **Fork** → 选自己的账号。
2. **fork 出来的仓库 Actions 默认是关的**：进 `Settings → Actions → General`，把
   **Allow all actions and reusable workflows** 选上，`Save`。
3. 同一页往下拉到 **Workflow permissions**，选 **Read and write permissions**（要让它能发 Release），`Save`。

### 方案 B：自己新建一个空仓库再搬文件

1. GitHub → **New repository** → 名字随意（例如 `my-obf`）→ **Private 或 Public 都行** → Create。
2. 下载官方仓库的 ZIP（Code → Download ZIP），解压后把 `.github/`、`ci/`、`src/`、`android/`、`tools/`、`kit.tar.gz`、`LICENSE`、`README.md` 一起拖进自己仓库的 **Add file → Upload files**，Commit。
   - 注意：**`.github` 是隐藏目录**，资源管理器里要打开「显示隐藏项目」才看得到；漏了它就不会有 Actions。
3. 同样去 `Settings → Actions → General` 打开 **Allow all actions**，并把 **Workflow permissions** 设为 **Read and write permissions**。

---

## 2. 造一个 Token（GUI 里要填的那个）

> GUI 需要一个能「写代码 + 触发 workflow」的 Token。**不要**用密码。

### 2.1 classic token（最简单）

1. 右上角头像 → **Settings** → 左栏最下 **Developer settings**
2. **Personal access tokens** → **Tokens (classic)** → **Generate new token (classic)**
3. Note 随便写（`nk-build`），Expiration 选 90 天或 No expiration
4. 勾这两个权限（其它都不用）：
   - ✅ `repo`（读写仓库、发 Release）
   - ✅ `workflow`（触发 / 取消 Actions）
5. **Generate token** → 复制那串 `ghp_xxxxxxxx...`
   ⚠️ 这串只显示一次，关掉页面就再也看不到，先粘到记事本。

### 2.2 fine-grained token（更安全，可选）

`Settings → Developer settings → Personal access tokens → Fine-grained tokens → Generate new token`：

| 项目 | 选择 |
|---|---|
| Repository access | Only select repositories → 选你那个仓库 |
| Permissions → Repository → Contents | Read and write |
| Permissions → Repository → Actions | Read and write |
| Permissions → Repository → Workflows | Read and write |

---

## 3. 桌面端怎么填

打开 `NKBuild-1.0-win64.exe` → 「⚙ 设置」页，五个框对应：

| 框 | 填什么 | 例子 | 怎么查 |
|---|---|---|---|
| GitHub Token | `ghp_` 开头那串 | `ghp_ab12…` | 第 2 步生成的那个 |
| 用户名 / 组织 | Token 属于谁 | `3495866893` | 浏览器打开 github.com，右上角头像下那一行 |
| 仓库 | 仓库名，**不带用户名** | `nk-obf` | 地址栏 `github.com/3495866893/nk-obf` 的最后一段 |
| 分支 | 默认分支 | `main` | 仓库首页左上角分支下拉框 |
| 工作流文件 | `.github/workflows/` 里的文件名 | `build-obf.yml` | 点开该文件 → Raw → 地址最后一段 |

填完点 **保存**，配置写到 `%APPDATA%\NKBuild\config.json`（只在本机，不联网外发）。
也可以直接手改这个 JSON：

```json
{
  "token": "ghp_xxxxxxxxxxxxxxxxxxxx",
  "owner": "3495866893",
  "repo": "nk-obf",
  "branch": "main",
  "workflow": "build-obf.yml",
  "src_root": "projects",
  "passes": ["string_encryption", "constant_encryption", "substitution", "bcf",
             "split_blocks", "indirect_branch", "flattening", "anti_debugging"],
  "last_out": "lucifer"
}
```

---

## 4. 手机端怎么填

App 底部 **设置** 页，同样五个框（每个框下面都写了当页提示）：

```
GitHub Token      ghp_…（第 2 步那串）
用户名 / 组织      3495866893
仓库              nk-obf
分支              main
工作流文件          build-obf.yml
```

Token 存在本机 `SharedPreferences`，不上传、不写日志。填完点 **保存**，回「构建」页选目录即可。

---

## 5. 第一次跑通（关键一步）

仓库是全新的、还没有 clang 缓存时，第一次必须让 CI 自己把 clang 建出来：

- **桌面端**：勾「重建 clang 工具链」（`rebuild_clang=true`）→ 开始编译。
- **手机端**：设置页同名的开关打开再编。
- **纯命令行**：

```bash
gh workflow run build-obf.yml -R 你的用户名/你的仓库 \
  -f src_dir=projects/probe \
  -f src_files=probe.c \
  -f presets="max mid" \
  -f out_name=hello \
  -f rebuild_clang=true          # 只在第一次需要
```

这一次大约 **40–100 分钟**（LLVM 21 全量编译）。结束后：

1. `clang` 工具链会作为 Release 资产 `llvm-build.tar.zst` 存下来（不占 Actions 配额）；
2. 后续每次构建会命中缓存，**1–3 分钟**出包；
3. 产物落在 **Release `nk-builds`** → `lucifer`（aarch64 PIE）。

---

## 6. 自检清单

| 检查 | 命令 / 位置 | 期望 |
|---|---|---|
| Token 有效 | 桌面端点「登录」，或 `curl -H "Authorization: Bearer $TOK" https://api.github.com/user` | 返回你的 `login` |
| 仓库可达 | `curl -H "Authorization: Bearer $TOK" https://api.github.com/repos/你/仓库` | `"private": true/false` 而不是 `Not Found` |
| workflow 存在 | `curl -H "Authorization: Bearer $TOK" https://api.github.com/repos/你/仓库/actions/workflows` | 列表里有 `build-obf.yml` |
| 首次构建 | Actions 页面 | `build lucifer` 绿勾，`成功: xxx.cfg (N 字节)` |
| 产物 | Releases → `nk-builds` | 有 `lucifer`，约 20 MB |

---

## 7. 常见报错对照

| 现象 | 原因 | 解决 |
|---|---|---|
| `401 Bad credentials` | Token 过期 / 复制少了几位 / 带了空格 | 重新生成，粘贴时去掉首尾空格 |
| `403 Resource not accessible` | Token 没勾 `workflow` | classic token 重勾 `repo`+`workflow` |
| `404 Not Found`（仓库明明存在） | 仓库名带了用户名，或分支写错 | 仓库只填名字；分支填 `main` |
| 422 `No ref found for ...` | 分支名不对 | 改成仓库真实默认分支（`master`/`main`） |
| `Failed to CreateArtifact: Artifact storage quota has been hit` | Actions 产物配额满 | 用 Release 存产物；清掉旧 artifact（本仓库的 workflow 已默认发 Release） |
| `guard (cache required)` 步骤红叉 | 没有 clang 缓存 | 勾「重建 clang 工具链」跑一次 |
| `imgui.cpp` 编译被 143 杀 | 内存炸（大 TU 全 pass） | 已内置：预处理 >800 KB 自动跳过 `flattening`，无需手改 |
| `Illegal instruction` 运行时 | 状态机兜底分支被落成 `brk` | 同上，跳过 `flattening` 即可 |
| APK 装不上（签名冲突） | 换了签名 | 先卸载旧版，或用自己的 keystore 重签 |

---

## 8. 想彻底去掉卡密 / 改默认值

- 桌面：删 `tools/NKBuild.py` 里的 `import xlverify`、`_build_license_bar()` 调用、`start()` 里那行 `if not self.lic_ok(): …`，以及 `lic_login / lic_paste / lic_unbind / lic_ok` 四个方法；`tools/xlverify.py` 可一并删。
- 手机：删 `android/.../XlVerify.kt`，并把 `MainActivity.kt` 里 `KamiGate(...)` 与 `vm.licAuto(...)` 两处注释掉。
- 改默认 token / 仓库：桌面改 `%APPDATA%\NKBuild\config.json`；手机改 `Prefs.kt` 里的默认值。

---

## 9. 授权提示

`src/`、`ci/`、`kit.tar.gz` 源自 **OLLVM-Next**（上游 **AGPL-3.0**）：你改了这些文件再对外提供（包括做在线服务）**必须公开对应源码**。
`tools/`、`android/`、`docs/` 是 MIT，随便用。详见根目录 `LICENSE`。
