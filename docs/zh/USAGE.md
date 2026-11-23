# 使用指南：桌面端、手机端、纯 CI

> 本篇讲：三种用法各自怎么走完一遍——桌面端每个卡片和按钮干什么、手机端每个页面放什么、纯命令行怎么 `workflow_dispatch`；token 存在哪、产物落在哪、常见报错怎么读。

三条路径共用同一套云端流水线，区别只是入口。桌面端和手机端都是「客户端」：上传源码 → 触发 Actions → 轮询 → 下载。纯 CI 用法跳过客户端，直接对仓库 dispatch。

## 通用前提

| 项 | 要求 |
| --- | --- |
| GitHub token | fine-grained，仅授权 `3495866893/nk-obf`，权限 `Contents: Read and write` + `Actions: Read` |
| 仓库 | 私有；Actions 必须允许运行（私有仓库按账户额度计费） |
| 源码 | 能在这台机器上读到的目录；编译单元与头文件都在一起 |
| 网络 | 手机端要能直连 `api.github.com` 与 `objects.githubusercontent.com` |

token 的边界：它能改这个仓库的任何文件、能读所有 run 日志。不要贴到聊天窗、不要提交进仓库；桌面端存在本地配置文件，手机端存在应用私有 `SharedPreferences`（`nkbuild`），卸载即清。

## 一、桌面端（Windows）

入口 `python tools/NKBuild.py`，或直接用打包好的 `NKBuild-1.0-win64.exe`（13.35 MB，无控制台窗口）。

界面是左四张卡片 + 右日志栏的布局：

| 区域 | 元素 | 怎么用 |
| --- | --- | --- |
| 卡片 1 · 源码 | 「选择目录」按钮 + 路径显示 | 选工程根目录；扫描结果会区分编译单元与待上传文件 |
| 卡片 2 · 工程·产物 | 工程名、产物名（默认 `lucifer`）、输出目录 | 产物名决定 Release 资产名；输出目录留空则按顺序回退到第一个可写的目录 |
| 卡片 3 · 反调试 | 「启用反调试」开关 + pass 勾选列表 | 关掉开关 = `NK_AD=0`（pass 仍开，空实现） |
| 卡片 4 · 开始 | 「开始编译」「终止」「打开产物」 | 开始后按钮变灰，进度条 + ETA 走起来 |
| 右侧 | 日志框（可滚动、自动跟随） | 上传进度、commit sha、run id、每一步的状态都在这 |

### 操作顺序

1. 填 token 与仓库（`owner/repo`），点「保存」。
2. 选源码目录，等扫描完成；确认「待上传文件」里包含头文件与资源。
3. 勾 pass（默认核心 8 项），决定反调试开关。
4. 点「开始编译」。
5. 看右侧日志：blob 上传 → tree → commit → dispatch → run 排队 → 编译 → 下载。
6. 点「打开产物」直接跳到产物所在目录。

### token 与配置存哪

桌面端的配置是一个 JSON，字段固定：

| 字段 | 含义 |
| --- | --- |
| `token` | GitHub PAT |
| `owner` / `repo` / `branch` | 仓库坐标，默认 `3495866893` / `nk-obf` / `main` |
| `workflow` | 默认 `build-obf.yml` |
| `src_root` | 上次选过的源码根目录，默认 `projects` |
| `passes` | 勾选的 pass key 列表 |
| `last_out` | 上次产物目录，「打开产物」按钮用它 |
| `kami` | 上次登录用的卡密 |

配置固定放在 `%APPDATA%\NKBuild\config.json`；`APPDATA` 不存在时退到 `~\NKBuild\config.json`。这个文件里是明文 token，别同步到网盘。

### 产物落哪

输出目录按顺序试写，第一个能建的胜出（每级都带 `NKBuild-out\<产物名>` 子目录）：

1. `桌面\NKBuild-out\<产物名>`
2. `文档\NKBuild-out\<产物名>`
3. `~\NKBuild-out\<产物名>`
4. `%LOCALAPPDATA%\NKBuild-out\<产物名>`
5. `%TEMP%\NKBuild-out\<产物名>`

全部失败时退回用户主目录。产物文件名形如 `lucifer`（或你填的产物名），旁边可能带 `nk-toolchain.tar.gz`。

### 上传细节（决定成功率的几个数）

| 项 | 行为 |
| --- | --- |
| 单文件上限 | `git/blob` 接口 100 MB；实测 1 / 2 / 5 / 20 MB 都返回 `201` |
| 重试 | blob 上传 4 次退避重试；失败才报错 |
| 配置上传 | 生成的 TOML 走 `contents` 接口（1 MB 限制，够用），落到 `ci/ui-<工程名>.toml` |
| 被 `#include` 的源文件 | 扫描后从编译列表剔除，但仍上传（否则头文件缺失） |
| 文件名带空格 | 会被 `SRC_FILES` 的空格切分搞坏，预检会直接标红 |

## 二、手机端（Android）

APK：`NKBuild-mobile-1.0-kami.apk`（9.98 MB），`minSdk 26` / `targetSdk 35`，固定签名 `android/keystore/nkbuild.p12`（PKCS12，自签 30 年），所以升级不会出现 `INSTALL_FAILED_UPDATE_INCOMPATIBLE`。

底部导航五页，每页一张皮肤背景图（`Crossfade` 切页换图）：

| 页面 | 内容 | 用途 |
| --- | --- | --- |
| 首页 | 工程名、源码目录、pass 摘要、开始按钮、进度 + ETA | 主操作台 |
| 内核 | 16 个 pass 分三组勾选（核心 8 / 进阶 5 / 不可用 3 灰掉） | 逐项选，本地生成 TOML |
| 产物 | 产物名、输出目录、任务历史卡片、一键安装 | 下载与安装 |
| 预检 | 危险文件清单（BLOCK / HIGH / NOTE 分级 + 原因） | 上传前先看，别等失败 |
| 设置 | token、仓库、分支、workflow、皮肤、模糊、自动导出、卡密、公告、检查更新 | 一次性配置 |

### 操作顺序

1. 设置页填 token；仓库默认 `3495866893`，仓库名默认还是旧名 `nk-plugin`，改成 `nk-obf`。
2. 首页选源码目录（SAF 授权，只读遍历）。
3. 内核页勾 pass；顶部「反调试」总开关与勾选列表是两件事。
4. 预检页扫一遍，BLOCK 项先处理掉。
5. 首页点「开始」，切后台也不会断——前台服务 `BuildService` 保活。
6. 产物页等下载完成，点「安装」。

### 手机端持久化的键

存在 `SharedPreferences("nkbuild")`，字段与桌面端一一对应：

| 键 | 类型 | 含义 |
| --- | --- | --- |
| `token` | string | GitHub PAT |
| `owner` / `repo` / `branch` / `workflow` | string | 仓库坐标与 workflow 文件 |
| `passes` | string | 逗号分隔的 pass key；空 = 首次启动用默认 8 个 |
| `antiDebug` | bool | 反调试总开关，默认 `true` |
| `autoExport` | bool | 产物自动存公共 `Download/NKBuild`，默认 `true` |
| `blur` | bool | 液态玻璃模糊；vivo/OPPO/realme/一加/iqoo 默认关（Shader 会崩） |
| `skin` | bool | 每页换皮肤图，默认 `true` |
| `run_id` / `job_proj` / `job_out` / `job_eta` / `job_start` | long/string | 断点续接用，见下 |
| `jobs` | JSON string | 任务历史数组，最多 30 条 |
| `kami` / `lic_vip` | string/long | 卡密与到期秒 |
| `notice` / `cloud_ver` / `cloud_info` / `cloud_url` | string | 公告与云端版本缓存 |

### 断点续接

上传开始前先落一个时间戳（`markJob`，此时 `run_id = 0`），拿到 run id 后补上。应用被系统杀掉再进来时：

1. 读 `run_id`；为 0 说明还没触发，从上传重来。
2. 不为 0 就直接轮询这个 run，进度、日志、产物都接着走。
3. 下载完成写 `markJobDone`，任务历史里那条变「已下载」。

任务历史每条记录的字段：

```json
{
  "run": 45041678195,
  "proj": "myproj",
  "out": "lucifer",
  "start": 1763800000000,
  "file": "lucifer",
  "done": true
}
```

点历史里任意一条 = 用它的 run id 重新下载，不重新编译。

### 产物落哪

- 公共目录 `Download/NKBuild/`（`autoExport = true` 时）
- 应用私有 `filesDir`（导出失败时的兜底）

「一键安装」走 `FileProvider`（`res/xml/file_paths.xml`），不需要「安装未知应用」以外的额外权限。

## 三、纯 CI（命令行 dispatch）

不用 GUI，直接对仓库触发 workflow。这也是桌面端/手机端底层做的事。

### 输入项（`build-obf.yml`）

| 输入 | 默认 | 说明 |
| --- | --- | --- |
| `src_dir` | `proj` | 仓库内源码目录 |
| `src_files` | `mian.cpp defense.c` | 空格分隔的源文件，相对 `src_dir` |
| `presets` | `max mid` | 预设尝试顺序，失败自动试下一个 |
| `defense_level` | `3` | `defense.c` 强度 0–3 |
| `config_file` | `ci/tuned.toml` | 复制成 `presets/<第一个 preset>.toml` 使用 |
| `cfg_list` | 空 | 一次跑多份配置，格式 `name:toml` 空格分隔 |
| `cfg_only` | 空 | 只用这一份 TOML，不套预设（IR 级 pass 才按 TOML 生效） |
| `anti_debug` | `true` | `false` 时 `NK_AD=0`（空实现，pass 仍开） |
| `rebuild_clang` | `false` | 缓存不可用时允许从头编 LLVM（约 100 分钟） |
| `skip_smoke` | `true` | 跳过 smoke test（省 4 分钟） |
| `out_name` | `lucifer` | 产物名 |

### 触发与跟踪

```bash
# 1) 触发
gh workflow run build-obf.yml --repo 3495866893/nk-obf \
  -f src_dir=proj \
  -f src_files="mian.cpp defense.c" \
  -f presets="max mid" \
  -f config_file=ci/tuned.toml \
  -f anti_debug=true \
  -f skip_smoke=true \
  -f out_name=lucifer

# 2) 找到刚触发的 run
gh run list --repo 3495866893/nk-obf --workflow build-obf.yml --limit 3

# 3) 盯着看
gh run watch <run-id> --repo 3495866893/nk-obf

# 4) 失败时拉日志
gh run view <run-id> --repo 3495866893/nk-obf --log-failed

# 5) 下载 artifact（保留 3 天）
gh run download <run-id> --repo 3495866893/nk-obf -n lucifer-obf
```

### 产物在哪

| 位置 | 内容 | 保留 |
| --- | --- | --- |
| Release `nk-builds` | `lucifer`、`lucifer-*`、`nk-toolchain.tar.gz` | 永久（每次 `--clobber` 覆盖） |
| artifact `lucifer-obf` | 同上的二进制 | 3 天 |
| artifact `nk-toolchain` | 工具链包 | 3 天 |
| artifact `nk-plugin-<kit指纹>` | `libNk.so` 压缩包，充当插件缓存 | 3 天 |

Release 资产不占 Actions 存储配额，artifact 占。所以长期分发走 Release，artifact 只当临时中转。

### 一次多份配置

```bash
gh workflow run build-obf.yml --repo 3495866893/nk-obf \
  -f src_dir=proj \
  -f src_files="mian.cpp" \
  -f cfg_list="c-adb:ci/c-adb.toml flat:ci/only-flat.toml indir:ci/only-indir.toml" \
  -f out_name=lucifer
```

每份配置编出一个 `lucifer-<name>`，互不影响；某一份失败会继续下一个，最后统一上传。

## 报错怎么读

| 现象 | 含义 | 处理 |
| --- | --- | --- |
| 退出码 `97` | `nk_ad` 判定命中，静默退出 | `NK_AD_DEBUG=1` 复现，看 `nk_ad.log` 是哪一项 |
| `143` / `runner has received a shutdown signal` | 被 SIGTERM：内存打满或 oomd 介入 | 看资源曲线 `mem_avail`，大 TU 降级已自动做，仍失败就拆文件 |
| `137` | OOM killer | 同上；确认没用 `chaos_state_machine` 之类吃内存的项 |
| `132` / `SIGILL` | 产物里有 `brk` / `udf` 被执行 | `flattening` 在大 TU 上的残留，确认阈值没被改 |
| `Undefined temporary symbol .L_v…` | 字符串加密的全局变量成了 thread-local | 已在阶段 A 修复；出现说明用了旧插件 |
| `duplicate symbol` | 被 `#include` 的 `.c/.cpp` 又被单独编译 | 扫描后剔除编译列表，或调整 `src_files` |
| `fatal error: 'imgui.h' file not found` | 头文件没上传 | 确认「待上传文件」包含头文件 |
| `Failed to CreateArtifact: Artifact storage quota has been hit` | artifact 配额爆了 | 别把 LLVM/NDK 往 artifact 传，走 Release |
| `LLVM 构建树缓存不可用` + 立即失败 | `guard` 步骤拦截 | 勾 `rebuild_clang=true` 重建一次（约 100 分钟） |
| 手机端 `code=179` | 账号未激活（服务端对未激活账号的返回值） | 1.0 起已去掉账号体系，只留卡密；老包还报这个就直接登卡密 |
| 「卡密已使用」 | 卡密一次性绑定到设备码 | 先「解绑」再登，或用同一账号回查 |
| 到期时间显示成 `4102243200` | 服务端把 `vip` 放在 `msg` 里且是字符串 | 这个数就是秒级时间戳，不是错误 |
| 上传卡在某个文件 | 单文件太大或名字带空格 | 预检页会标 BLOCK，按提示改名/剔除 |
| 编译成功但产物没变化 | 插件没生效 | 确认 `NK=1` 与 `NK_CONFIG` 指向的 TOML 被读到 |

## 环境变量速查（手动编译时用）

| 变量 | 作用 |
| --- | --- |
| `NK=1` | 插件总开关；不设则加载但不改写 |
| `NK_CONFIG=<toml>` | 配置文件路径 |
| `NK_PRESET=<name>` | 直接指定预设（等价 TOML 里 `preset`） |
| `NK_AD=1/0` | 链接真实看门狗 / 编成空实现 |
| `NK_AD_DEBUG=1` | 运行时打印每一项判定 |
| `NK_AD_WLOCK=1` | `.text` 只读保护 |
| `NK_AD_EMU=0` | 关闭反模拟器整组 |

环境变量统一 `NK_` 前缀，`ci/build-lucifer.sh` 里就是 `export NK=1 NK_CONFIG="$cfg"`，预设另用 `NK_PRESET`。反调试相关的几个开关只在 `nk_ad.c` 里读，与 pass 配置互不影响。
