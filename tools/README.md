# tools · 桌面端与授权模块

| 文件 | 说明 |
|---|---|
| `NKBuild.py` | Windows 桌面 GUI（CustomTkinter）：选目录 → 选 pass → 上传 GitHub → 触发 Actions → 轮询 → 下载产物到 `Download/NKBuild`；顶部带卡密授权栏 |
| `xlverify.py` | 卡密服务客户端（纯 Python，无第三方依赖）：SM4-CBC + HMAC-SM3 信封、设备码、登录/解绑 |

运行：

```bash
python -m pip install customtkinter
python NKBuild.py
```

打包成单文件 exe：

```bash
python -m PyInstaller --noconfirm --onefile --noconsole --name NKBuild ^
  --exclude-module numpy --exclude-module scipy --exclude-module matplotlib ^
  --exclude-module pandas --exclude-module yaml --exclude-module PIL NKBuild.py
```

## 卡密授权（可整段删掉）

`xlverify.py` 只负责向前端服务器验证卡密；`NKBuild.py` 里与它相关的只有四处：

1. 顶部 `import xlverify as XL`
2. `App.__init__` 里的 `_lic_state / _lic_text / _lic_km`
3. `_build_license_bar()` 与 `lic_login / lic_paste / lic_unbind / lic_ok`
4. `start()` 开头的 `if not self.lic_ok(): ...` 以及 `_drain()` 里的 `lic` / `licmsg` 分支

把这四处删掉（或让 `lic_ok()` 直接 `return True`）就彻底没有卡密了。
