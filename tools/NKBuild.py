# -*- coding: utf-8 -*-
"""NKBuild v6 —— 源码一键云端混淆编译（重建版）

工作区被误删后按原功能重建：
  ① 源码目录 + 自动扫描
  ② 混淆强度预设 + 16 项 pass 逐项勾选（崩溃项置灰）+ 自动生成/上传 TOML
  ③ Ensia 反调试开关（对应 CI 的 anti_debug 输入）
  ④ 实时日志 + 进度 + 已用时间 + 预估剩余 + 终止 + 打开产物

依赖：customtkinter（pip install customtkinter）
"""
import base64
import json
import math
import os
import queue
import sys
import threading
import time
import tkinter as tk
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from tkinter import filedialog, messagebox

try:
    import customtkinter as ctk
except Exception:
    ctk = None

# XL 云验证（与 xl_verify.h 同协议：SM4-CBC + HMAC-SM3 + MD5 check）
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    import xlverify as XL
except Exception:                                    # noqa: BLE001
    XL = None

# ---------------------------------------------------------------- 外观常量
BG = "#0e1018"
CARD = "#161a26"
CARD_ALT = "#1d2231"
ACCENT = "#7c5cff"
ACCENT_HOVER = "#8f74ff"
TEXT = "#e8eaf2"
SUBTLE = "#8b90a6"
GREEN = "#4bd37b"
RED = "#ef5b6b"
FONT_UI = "Microsoft YaHei UI"
FONT_MONO = "Consolas"

CFG_DIR = os.path.join(os.environ.get("APPDATA", os.path.expanduser("~")), "NKBuild")
CFG_FILE = os.path.join(CFG_DIR, "config.json")

DEFAULT_CFG = {
    "token": "", "owner": "3495866893", "repo": "nk-obf",
    "branch": "main", "workflow": "build-obf.yml", "src_root": "projects",
    "passes": [], "last_out": "",
    "kami": "",            # 上次登录用的卡密（只存本机 %APPDATA%\NKBuild\config.json）
}

# ---------------------------------------------------------------- pass 表
# (key, 中文名, 说明, 默认勾选, 可选)
PASS_SPEC = [
    ("string_encryption", "字符串加密", "字面量运行时解密，解密后清零", True, True),
    ("constant_encryption", "常量加密", "常量 globalize + XOR（feistel 永久关）", True, True),
    ("substitution", "指令替换", "等价指令序列替换", True, True),
    ("bcf", "虚假控制流", "不透明谓词 + 假分支", True, True),
    ("split_blocks", "基本块分割", "每函数切 3 段", True, True),
    ("indirect_branch", "间接跳转", "栈上跳转表 + 目标加密", True, True),
    ("flattening", "控制流平坦化", "状态机 + 分发器", True, True),
    ("anti_debugging", "反调试（安全版）", "由 ③ 开关控制，勾了也要 ③ 开才生效", True, True),
    ("mba", "MBA 混淆", "位运算恒等式替换，体积大", False, True),
    ("chaos_state_machine", "混沌状态机", "未充分验证", False, True),
    ("vector_obfuscation", "向量混淆", "体积暴涨、收益低", False, True),
    ("function_wrapper", "函数包装", "可能破坏结构", False, True),
    ("function_call_obfuscate", "函数调用混淆", "难排查崩溃", False, True),
    ("anti_hooking", "反 Hook", "已知会崩（基线不一致），禁用", False, False),
    ("anti_class_dump", "类 dump 防护", "仅 Objective-C", False, False),
    ("objc", "ObjC 混淆", "仅 Objective-C", False, False),
]
DEFAULT_PASSES = [k for k, _, _, on, ok in PASS_SPEC if on and ok]


def build_toml(selected):
    sel = set(selected)
    L = ['[global]', 'preset = ""', 'seed = "0xDEADBEEF"', 'verbose = false', '']
    for key, _label, _desc, _on, ok in PASS_SPEC:
        on = (key in sel) and ok
        L.append("[passes.%s]" % key)
        L.append("enabled = %s" % ("true" if on else "false"))
        if on and key == "string_encryption":
            L += ['probability = 100', 'anti_dump = true', 'force_content = [".*"]']
        elif on and key == "constant_encryption":
            L += ['iterations = 2', 'share_count = 6', 'feistel = false',
                  'globalize = true', 'globalize_prob = 100',
                  'substitute_xor = true', 'substitute_xor_prob = 100',
                  'skip_value = ["^0x0$"]']
        elif on and key == "substitution":
            L += ['probability = 60', 'iterations = 1']
        elif on and key == "bcf":
            L += ['probability = 60', 'iterations = 1', 'complexity = 6',
                  'entropy_chain = false', 'junk_asm = false',
                  'nested = false', 'create_func = false']
        elif on and key == "split_blocks":
            L += ['splits = 3', 'stack_confusion = false']
        elif on and key == "indirect_branch":
            L += ['use_stack = true', 'enc_jump_target = true']
        elif on and key in ("anti_debugging", "anti_hooking"):
            L += ['probability = 100']
        L.append('')
    return "\n".join(L)


def load_cfg():
    cfg = dict(DEFAULT_CFG)
    if os.path.isfile(CFG_FILE):
        try:
            cfg.update(json.load(open(CFG_FILE, encoding="utf-8")))
        except Exception:
            pass
    if not cfg.get("passes"):
        cfg["passes"] = list(DEFAULT_PASSES)
    return cfg


def save_cfg(cfg):
    try:
        os.makedirs(CFG_DIR, exist_ok=True)
        json.dump(cfg, open(CFG_FILE, "w", encoding="utf-8"),
                  ensure_ascii=False, indent=2)
    except Exception:
        pass


# ---------------------------------------------------------------- GitHub API
class Github:
    def __init__(self, token):
        self.token = token
        self.opener = urllib.request.build_opener(
            urllib.request.ProxyHandler({}), _NoAuthRedirect())

    def _req(self, path, method="GET", body=None, base="https://api.github.com"):
        url = path if path.startswith("http") else base + path
        data = json.dumps(body).encode() if body is not None else None
        r = urllib.request.Request(url, method=method, data=data)
        r.add_header("Authorization", "Bearer " + self.token)
        r.add_header("Accept", "application/vnd.github+json")
        r.add_header("User-Agent", "NKBuild")
        r.add_header("Accept-Encoding", "identity")
        if body is not None:
            r.add_header("Content-Type", "application/json")
        try:
            with self.opener.open(r, timeout=180) as x:
                raw = x.read()
        except urllib.error.HTTPError as e:
            detail = e.read()[:300].decode("utf-8", "replace")
            raise RuntimeError("HTTP %s %s: %s" % (e.code, path, detail))
        if not raw:
            return {}
        try:
            return json.loads(raw.decode())
        except Exception:
            return {}

    # --- 基础 ---
    def user(self):
        return self._req("/user")

    def head(self, o, r, branch):
        d = self._req("/repos/%s/%s/git/ref/heads/%s" % (o, r, branch))
        return d["object"]["sha"], d["object"]["sha"]

    def tree(self, o, r, base_sha, entries):
        d = self._req("/repos/%s/%s/git/trees" % (o, r), "POST",
                      {"base_tree": base_sha, "tree": entries})
        return d["sha"]

    def blob(self, o, r, content):
        """git/blob 接口：单文件上限 100 MB，**没有 1 MB 限制**（contents 接口才有）。
        网络抖动就退避重试，大文件尤其怕一次超时把整次上传废掉。"""
        body = {"content": base64.b64encode(content).decode(), "encoding": "base64"}
        last = None
        for i in range(4):
            try:
                return self._req("/repos/%s/%s/git/blobs" % (o, r), "POST", body)["sha"]
            except Exception as e:
                last = e
                if "HTTP 4" in str(e) or i == 3:
                    raise
                time.sleep(1.2 * (i + 1))
        raise last

    def commit(self, o, r, msg, tree_sha, parents):
        d = self._req("/repos/%s/%s/git/commits" % (o, r), "POST",
                      {"message": msg, "tree": tree_sha, "parents": parents})
        return d["sha"]

    def ref(self, o, r, branch, sha):
        self._req("/repos/%s/%s/git/refs/heads/%s" % (o, r, branch),
                  "PATCH", {"sha": sha, "force": True})

    def put_file(self, o, r, path, text, msg):
        url = "/repos/%s/%s/contents/%s" % (o, r, path)
        sha = None
        try:
            sha = self._req(url).get("sha")
        except RuntimeError:
            sha = None
        body = {"message": msg, "branch": "main",
                "content": base64.b64encode(text.encode()).decode()}
        if sha:
            body["sha"] = sha
        self._req(url, "PUT", body)

    # --- Actions ---
    def dispatch(self, o, r, wf, ref, inputs):
        self._req("/repos/%s/%s/actions/workflows/%s/dispatches" % (o, r, wf),
                  "POST", {"ref": ref, "inputs": inputs})

    def latest(self, o, r, wf):
        d = self._req("/repos/%s/%s/actions/workflows/%s/runs?per_page=1"
                      % (o, r, wf))
        runs = d.get("workflow_runs") or []
        return runs[0] if runs else None

    def run(self, o, r, rid):
        return self._req("/repos/%s/%s/actions/runs/%d" % (o, r, rid))

    def jobs(self, o, r, rid):
        return (self._req("/repos/%s/%s/actions/runs/%d/jobs" % (o, r, rid))
                or {}).get("jobs", [])

    def arts(self, o, r, rid):
        return (self._req("/repos/%s/%s/actions/runs/%d/artifacts" % (o, r, rid))
                or {}).get("artifacts", [])

    def cancel_run(self, o, r, rid):
        return self._req("/repos/%s/%s/actions/runs/%d/cancel" % (o, r, rid),
                         "POST")

    def download_art(self, o, r, aid):
        url = "https://api.github.com/repos/%s/%s/actions/artifacts/%d/zip" % (o, r, aid)
        req = urllib.request.Request(url)
        req.add_header("Authorization", "Bearer " + self.token)
        req.add_header("User-Agent", "NKBuild")
        with self.opener.open(req, timeout=600) as x:
            return x.read()


class _NoAuthRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        new = super().redirect_request(req, fp, code, msg, headers, newurl)
        if new is not None:
            new.headers.pop("Authorization", None)
            new.unredirected_hdrs.pop("Authorization", None)
        return new


# ---------------------------------------------------------------- 配置向导
def _ask(prompt, default=""):
    """优先命令行；无控制台时用 Tk 输入框"""
    stdin_ok = False
    try:
        stdin_ok = sys.stdin is not None and sys.stdin.isatty()
    except Exception:
        stdin_ok = False
    if stdin_ok:
        try:
            v = input("%s [%s]: " % (prompt, default)).strip()
        except (EOFError, KeyboardInterrupt):
            v = ""
        return v or default
    try:
        dlg = ctk.CTkInputDialog(
            text="%s\n（留空用默认：%s）" % (prompt, default or "无"),
            title="NKBuild 初始化")
        return (dlg.get_input() or "").strip() or default
    except Exception:
        return default


def setup_wizard():
    cfg = load_cfg()
    print("==== NKBuild 首次运行初始化 ====")
    cfg["owner"] = _ask("GitHub 用户名/组织（Token 属于谁，例 3495866893）",
                        cfg.get("owner") or "3495866893")
    cfg["repo"] = _ask("仓库名（只填名字，不带用户名，例 nk-obf）",
                       cfg.get("repo") or "nk-obf")
    cfg["branch"] = _ask("分支（一般是 main）", cfg.get("branch") or "main")
    cfg["workflow"] = _ask("工作流文件（.github/workflows/ 下的文件名，例 build-obf.yml）",
                           cfg.get("workflow") or "build-obf.yml")
    tok = _ask("Token（classic，勾 repo+workflow）", "")
    if tok:
        cfg["token"] = tok
    save_cfg(cfg)
    print("已保存:", CFG_FILE)
    return cfg


# ---------------------------------------------------------------- 主窗口
class App(ctk.CTk if ctk else object):
    def __init__(self):
        if ctk is None:
            raise SystemExit("缺少 customtkinter：pip install customtkinter")
        ctk.set_appearance_mode("dark")
        super().__init__()
        self.title("NKBuild · 云端混淆编译")
        sw, sh = self.winfo_screenwidth(), self.winfo_screenheight()
        w, h = min(1080, sw - 80), min(820, sh - 100)
        self.geometry("%dx%d+%d+%d" % (w, h, max(0, (sw - w) // 2),
                                       max(0, (sh - h) // 3)))
        self.minsize(940, 680)
        self.configure(fg_color=BG)

        self.cfg = load_cfg()
        self.sel_passes = list(self.cfg.get("passes") or DEFAULT_PASSES)
        self.q = queue.Queue()
        self.worker = None
        self._busy = False
        self._t0 = None
        self._run_id = None
        self._cancel = threading.Event()
        self._status_text = ""
        self._eta_total = 0
        self._prog = 0.0
        self._prog_target = 0.0
        self._last_out = self.cfg.get("last_out") or ""
        # 授权状态：ok / expiring / expired / checking / none
        self._lic_state = "none"
        self._lic_text = "未登录"
        self._lic_km = self.cfg.get("kami") or ""

        self._build_ui()
        self.after(120, self._drain)
        self.after(200, self._tick)

    # ---------------- 仓库设置弹窗 ----------------
    def open_settings(self):
        """五个字段各带一行保姆级提示，改完直接写回 config.json。"""
        if getattr(self, "_set_win", None) and self._set_win.winfo_exists():
            self._set_win.focus()
            return
        win = ctk.CTkToplevel(self)
        self._set_win = win
        win.title("仓库设置 · 该填什么")
        win.geometry("620x560")
        win.configure(fg_color=BG)
        win.transient(self)

        hint = [
            ("GitHub Token", "token", "ghp_ 开头那串。GitHub → Settings → Developer settings → "
                                      "Personal access tokens → Tokens (classic) → 勾 repo + workflow"),
            ("用户名 / 组织", "owner", "Token 属于谁就填谁，例如 3495866893（头像菜单里能看到）"),
            ("仓库", "repo", "只填仓库名，不带用户名。github.com/3495866893/nk-obf 就填 nk-obf"),
            ("分支", "branch", "默认分支，一般是 main（也可能是 master）"),
            ("工作流文件", "workflow", "仓库 .github/workflows/ 下的文件名，例如 build-obf.yml"),
        ]
        vars_ = {}
        for label, key, tip in hint:
            ctk.CTkLabel(win, text=label, text_color=TEXT, anchor="w",
                         font=ctk.CTkFont(family=FONT_UI, size=12, weight="bold")
                         ).pack(fill="x", padx=18, pady=(12, 2))
            v = tk.StringVar(value=str(self.cfg.get(key) or ""))
            vars_[key] = v
            ctk.CTkEntry(win, textvariable=v, fg_color=CARD_ALT, border_width=0, height=32,
                         font=ctk.CTkFont(family=FONT_MONO, size=12)
                         ).pack(fill="x", padx=18)
            ctk.CTkLabel(win, text=tip, text_color=SUBTLE, anchor="w", justify="left",
                         wraplength=560,
                         font=ctk.CTkFont(family=FONT_UI, size=10)
                         ).pack(fill="x", padx=18, pady=(2, 0))

        def save():
            for k, v in vars_.items():
                self.cfg[k] = v.get().strip()
            if not self.cfg.get("branch"):
                self.cfg["branch"] = "main"
            if not self.cfg.get("workflow"):
                self.cfg["workflow"] = "build-obf.yml"
            save_cfg(self.cfg)
            self.logline("已保存仓库设置：%s/%s · 分支 %s · 工作流 %s"
                         % (self.cfg["owner"], self.cfg["repo"],
                            self.cfg["branch"], self.cfg["workflow"]))
            win.destroy()

        bar = ctk.CTkFrame(win, fg_color="transparent")
        bar.pack(fill="x", padx=18, pady=16)
        ctk.CTkButton(bar, text="保存", width=110, height=34, corner_radius=9,
                      fg_color=ACCENT, hover_color=ACCENT_HOVER,
                      font=ctk.CTkFont(family=FONT_UI, size=13), command=save
                      ).pack(side="left")
        ctk.CTkLabel(bar, text="配置写在 %APPDATA%\\NKBuild\\config.json，只在本机",
                     text_color=SUBTLE,
                     font=ctk.CTkFont(family=FONT_UI, size=10)).pack(side="left", padx=(12, 0))

    # ---------------- 授权（XL 云验证） ----------------
    def _build_license_bar(self):
        bar = ctk.CTkFrame(self, fg_color=CARD, corner_radius=12)
        bar.pack(fill="x", padx=18, pady=(0, 8))
        self.lic_dot = ctk.CTkLabel(bar, text="●", text_color=SUBTLE,
                                    font=ctk.CTkFont(family=FONT_UI, size=14))
        self.lic_dot.pack(side="left", padx=(14, 4), pady=10)
        ctk.CTkLabel(bar, text="授权", text_color=TEXT,
                     font=ctk.CTkFont(family=FONT_UI, size=12, weight="bold")
                     ).pack(side="left", padx=(0, 10))
        self.var_km = tk.StringVar(value=self._lic_km)
        self.ent_km = ctk.CTkEntry(bar, textvariable=self.var_km, show="*",
                                   placeholder_text="卡密（例：XXXX-XXXX-XXXX）",
                                   fg_color=CARD_ALT, border_width=0, width=260,
                                   font=ctk.CTkFont(family=FONT_MONO, size=12))
        self.ent_km.pack(side="left", padx=(0, 8), pady=10)
        self.btn_login = ctk.CTkButton(bar, text="登录", width=70, height=30,
                                       corner_radius=9, fg_color=ACCENT,
                                       hover_color=ACCENT_HOVER,
                                       font=ctk.CTkFont(family=FONT_UI, size=12),
                                       command=self.lic_login)
        self.btn_login.pack(side="left", padx=(0, 6))
        ctk.CTkButton(bar, text="粘贴", width=60, height=30, corner_radius=9,
                      fg_color=CARD_ALT, hover_color="#2b3354",
                      font=ctk.CTkFont(family=FONT_UI, size=12),
                      command=self.lic_paste).pack(side="left", padx=(0, 6))
        ctk.CTkButton(bar, text="解绑", width=60, height=30, corner_radius=9,
                      fg_color=CARD_ALT, hover_color="#2b3354",
                      font=ctk.CTkFont(family=FONT_UI, size=12),
                      command=self.lic_unbind).pack(side="left")
        self.lic_lbl = ctk.CTkLabel(bar, text=self._lic_text, text_color=SUBTLE,
                                    font=ctk.CTkFont(family=FONT_UI, size=12))
        self.lic_lbl.pack(side="right", padx=14)
        # 启动后自动拿上次的卡密验一次
        if self._lic_km:
            self.after(400, self.lic_login)

    def _lic_apply(self, state, text):
        self._lic_state = state
        self._lic_text = text
        color = {"ok": GREEN, "expiring": "#f7c948", "expired": RED,
                 "checking": SUBTLE, "none": RED}.get(state, SUBTLE)
        try:
            self.lic_dot.configure(text_color=color)
            self.lic_lbl.configure(text=text, text_color=color)
        except Exception:                            # noqa: BLE001
            pass

    def lic_login(self):
        if XL is None:
            self._lic_apply("expired", "缺少 xlverify.py")
            return
        km = (self.var_km.get() or "").strip()
        if not km:
            messagebox.showwarning("NKBuild", "先填卡密")
            return
        self._lic_apply("checking", "验证中…")
        self.btn_login.configure(state="disabled", text="…")

        def run():
            r = XL.login(km)
            if not r["ok"]:
                self.q.put(("lic", (False, r["msg"], km, r.get("vip") or 0)))
                return
            vip = int(r.get("vip") or 0)
            if vip > 0:
                days = int((vip - __import__("time").time()) // 86400)
                state = "ok" if days >= 30 else ("expiring" if days >= 7 else "expired")
                text = "已授权 · %s（剩 %d 天）" % (r["expiry"], days)
                if days < 0:
                    self.q.put(("lic", (False, "卡密已过期", km, vip)))
                    return
            else:
                state, text = "ok", "已授权 · 永久"
            self.q.put(("lic", (True, text, km, vip, state)))

        threading.Thread(target=run, daemon=True).start()

    def lic_paste(self):
        try:
            t = self.clipboard_get()
        except Exception:                            # noqa: BLE001
            t = ""
        t = "".join(ch for ch in (t or "") if ch not in " \r\n\t")
        if len(t) < 4:
            messagebox.showwarning("NKBuild", "剪贴板里没有像卡密的内容")
            return
        self.var_km.set(t)
        self.lic_login()

    def lic_unbind(self):
        if XL is None:
            return
        km = (self.var_km.get() or "").strip()
        if not km:
            messagebox.showwarning("NKBuild", "先填卡密再解绑")
            return
        if not messagebox.askyesno("NKBuild", "解绑后本机就不能再用这张卡密，确定？"):
            return

        def run():
            r = XL.unbind(km)
            self.q.put(("licmsg", r["msg"]))
        threading.Thread(target=run, daemon=True).start()

    def lic_ok(self):
        return self._lic_state in ("ok", "expiring")

    # ---------------- UI ----------------
    def _card(self, parent, title):
        c = ctk.CTkFrame(parent, fg_color=CARD, corner_radius=14)
        c.pack(fill="x", pady=(0, 10))
        ctk.CTkLabel(c, text=title, text_color=TEXT, anchor="w",
                     font=ctk.CTkFont(family=FONT_UI, size=13,
                                      weight="bold")).pack(fill="x", padx=16, pady=(12, 2))
        return c

    def _build_ui(self):
        head = ctk.CTkFrame(self, fg_color="transparent")
        head.pack(fill="x", padx=18, pady=(14, 6))
        brand = ctk.CTkFrame(head, fg_color="transparent")
        brand.pack(side="left")
        ctk.CTkLabel(brand, text="NK", text_color=ACCENT,
                     font=ctk.CTkFont(family=FONT_UI, size=34, weight="bold")
                     ).pack(side="left")
        col = ctk.CTkFrame(brand, fg_color="transparent")
        col.pack(side="left", padx=(10, 0))
        ctk.CTkLabel(col, text="OLLVM-NEXT", text_color=TEXT,
                     font=ctk.CTkFont(family=FONT_UI, size=15, weight="bold")
                     ).pack(anchor="w")
        ctk.CTkLabel(col, text="新一代混淆 · 源码 → 云端编译", text_color=ACCENT_HOVER,
                     font=ctk.CTkFont(family=FONT_UI, size=11)
                     ).pack(anchor="w")
        ctk.CTkButton(head, text="⚙ 仓库设置", width=96, height=30, corner_radius=9,
                      fg_color=CARD_ALT, hover_color="#2b3354",
                      font=ctk.CTkFont(family=FONT_UI, size=12),
                      command=self.open_settings).pack(side="left", padx=(16, 0))
        self.dot = ctk.CTkLabel(head, text="● 待机", text_color=SUBTLE,
                                font=ctk.CTkFont(family=FONT_UI, size=12))
        self.dot.pack(side="right")

        self._build_license_bar()

        body = ctk.CTkFrame(self, fg_color="transparent")
        body.pack(fill="both", expand=True, padx=18, pady=(0, 6))
        left = ctk.CTkFrame(body, fg_color="transparent", width=430)
        left.pack(side="left", fill="both", padx=(0, 12))
        left.pack_propagate(False)
        right = ctk.CTkFrame(body, fg_color="transparent")
        right.pack(side="left", fill="both", expand=True)

        # ① 源码
        c1 = self._card(left, "① 源码")
        row = ctk.CTkFrame(c1, fg_color="transparent")
        row.pack(fill="x", padx=14, pady=(4, 6))
        self.var_src = tk.StringVar(value="")
        ctk.CTkEntry(row, textvariable=self.var_src, placeholder_text="选择源码目录",
                     fg_color=CARD_ALT, border_width=0,
                     font=ctk.CTkFont(family=FONT_UI, size=12)
                     ).pack(side="left", fill="x", expand=True, padx=(0, 8))
        ctk.CTkButton(row, text="浏览", width=64, height=30, corner_radius=9,
                      fg_color=CARD_ALT, hover_color="#2b3354",
                      font=ctk.CTkFont(family=FONT_UI, size=12),
                      command=self.pick_dir).pack(side="left")
        row2 = ctk.CTkFrame(c1, fg_color="transparent")
        row2.pack(fill="x", padx=14, pady=(0, 6))
        self.var_name = tk.StringVar(value="myproj")
        self.var_out = tk.StringVar(value="lucifer")
        for label, var in (("工程名", self.var_name), ("产物名", self.var_out)):
            ctk.CTkLabel(row2, text=label, text_color=SUBTLE,
                         font=ctk.CTkFont(family=FONT_UI, size=11)).pack(side="left")
            ctk.CTkEntry(row2, textvariable=var, width=110, fg_color=CARD_ALT,
                         border_width=0, font=ctk.CTkFont(family=FONT_UI, size=12)
                         ).pack(side="left", padx=(4, 12))
        ctk.CTkButton(c1, text="自动扫描编译文件", height=32, corner_radius=9,
                      fg_color=CARD_ALT, hover_color="#2b3354",
                      font=ctk.CTkFont(family=FONT_UI, size=12),
                      command=self.scan).pack(fill="x", padx=14, pady=(0, 12))

        # ② 混淆
        c2 = self._card(left, "② 混淆强度")
        self.seg = ctk.CTkSegmentedButton(
            c2, values=["标准", "轻量"], height=32, corner_radius=9,
            fg_color=CARD_ALT, selected_color=ACCENT, selected_hover_color=ACCENT_HOVER,
            unselected_color=CARD_ALT, unselected_hover_color="#242a44",
            text_color=TEXT, font=ctk.CTkFont(family=FONT_UI, size=12),
            command=self._on_seg)
        self.seg.pack(fill="x", padx=14, pady=(2, 6))
        self.seg.set("标准")
        self.lbl_pass = ctk.CTkLabel(c2, text=self._pass_text(), text_color=SUBTLE,
                                     anchor="w", font=ctk.CTkFont(family=FONT_UI, size=11))
        self.lbl_pass.pack(fill="x", padx=16)
        ctk.CTkButton(c2, text="混淆选项…（逐项勾选）", height=32, corner_radius=9,
                      fg_color=CARD_ALT, hover_color="#2b3354", text_color=TEXT,
                      font=ctk.CTkFont(family=FONT_UI, size=12),
                      command=self.open_passes).pack(fill="x", padx=14, pady=(4, 12))

        # ③ 反调试
        c3 = self._card(left, "③ Ensia 反调试")
        r3 = ctk.CTkFrame(c3, fg_color="transparent")
        r3.pack(fill="x", padx=16, pady=(2, 12))
        self.var_ad = tk.BooleanVar(value=True)
        ctk.CTkLabel(r3, text="启用运行时反调试（nk_ad 看门狗）", text_color=TEXT,
                     font=ctk.CTkFont(family=FONT_UI, size=12)).pack(side="left")
        ctk.CTkSwitch(r3, text="", variable=self.var_ad, width=48,
                      progress_color=ACCENT, button_color="#fff").pack(side="right")

        # 底栏
        bottom = ctk.CTkFrame(self, fg_color="transparent")
        bottom.pack(fill="x", padx=24, pady=(2, 14))
        self.pbar = ctk.CTkProgressBar(bottom, height=10, corner_radius=5,
                                       progress_color=ACCENT, fg_color=CARD_ALT)
        self.pbar.pack(side="left", fill="x", expand=True, padx=(0, 12))
        self.pbar.set(0)
        self.btn_go = ctk.CTkButton(bottom, text="开始编译", width=140, height=40,
                                    corner_radius=12, fg_color=ACCENT,
                                    hover_color=ACCENT_HOVER,
                                    font=ctk.CTkFont(family=FONT_UI, size=14,
                                                     weight="bold"),
                                    command=self.start)
        self.btn_go.pack(side="right")
        self.btn_abort = ctk.CTkButton(bottom, text="终止", width=84, height=40,
                                       corner_radius=12, fg_color="#8E2F3A",
                                       hover_color="#B03946", text_color="#FFEDEF",
                                       font=ctk.CTkFont(family=FONT_UI, size=12,
                                                        weight="bold"),
                                       state="disabled", command=self.abort)
        self.btn_abort.pack(side="right", padx=(0, 8))
        self.btn_open = ctk.CTkButton(bottom, text="打开产物", width=96, height=40,
                                      corner_radius=12, fg_color=CARD_ALT,
                                      hover_color="#2b3354", text_color=TEXT,
                                      font=ctk.CTkFont(family=FONT_UI, size=12),
                                      command=self.open_out)
        self.btn_open.pack(side="right", padx=(0, 8))
        self.lbl_pct = ctk.CTkLabel(bottom, text="0%", width=44, text_color=SUBTLE,
                                    font=ctk.CTkFont(family=FONT_MONO, size=12))
        self.lbl_pct.pack(side="right", padx=(0, 10))

        # ④ 日志
        c4 = ctk.CTkFrame(right, fg_color=CARD, corner_radius=14)
        c4.pack(fill="both", expand=True)
        ctk.CTkLabel(c4, text="④ 日志", text_color=TEXT, anchor="w",
                     font=ctk.CTkFont(family=FONT_UI, size=13, weight="bold")
                     ).pack(fill="x", padx=16, pady=(12, 4))
        self.log = ctk.CTkTextbox(c4, fg_color=CARD_ALT, corner_radius=10,
                                  text_color=TEXT, font=ctk.CTkFont(family=FONT_MONO, size=11))
        self.log.pack(fill="both", expand=True, padx=12, pady=(0, 12))

    # ---------------- 小工具 ----------------
    def logline(self, s):
        self.q.put(("log", str(s)))

    def status(self, s):
        self.q.put(("st", s))

    def set_prog(self, v):
        self.q.put(("pg", max(0, min(100, v)) / 100.0))

    def _pass_text(self):
        return "已选 %d / %d 项（灰色=已知会崩，不可选）" % (
            len(self.sel_passes), len(PASS_SPEC))

    def _on_seg(self, value):
        if value == "标准":
            self.sel_passes = list(DEFAULT_PASSES)
        else:
            self.sel_passes = ["string_encryption", "substitution", "bcf", "split_blocks"]
        self.cfg["passes"] = list(self.sel_passes)
        save_cfg(self.cfg)
        if hasattr(self, "lbl_pass"):
            self.lbl_pass.configure(text=self._pass_text())
        self.logline("档位：%s（%d 项）" % (value, len(self.sel_passes)))

    # ---------------- 选项窗口 ----------------
    def open_passes(self):
        if not hasattr(self, "sel_passes"):
            self.sel_passes = list(DEFAULT_PASSES)
        w = ctk.CTkToplevel(self)
        w.title("NKBuild · 混淆选项")
        w.geometry("560x660")
        w.configure(fg_color=BG)
        w.transient(self)
        w.attributes("-topmost", True)
        ctk.CTkLabel(w, text="勾选要启用的 pass", text_color=TEXT,
                     font=ctk.CTkFont(family=FONT_UI, size=14, weight="bold")
                     ).pack(anchor="w", padx=18, pady=(14, 2))
        ctk.CTkLabel(w, text="灰色 = 已知会崩或仅适用 ObjC", text_color=SUBTLE,
                     font=ctk.CTkFont(family=FONT_UI, size=11)
                     ).pack(anchor="w", padx=18, pady=(0, 8))
        box = ctk.CTkScrollableFrame(w, fg_color=CARD, corner_radius=12)
        box.pack(fill="both", expand=True, padx=14, pady=(0, 8))
        vars_ = {}
        for key, label, desc, _on, ok in PASS_SPEC:
            row = ctk.CTkFrame(box, fg_color="transparent")
            row.pack(fill="x", pady=3)
            v = tk.BooleanVar(value=(key in self.sel_passes and ok))
            ctk.CTkCheckBox(row, text="", variable=v, width=24, checkbox_width=20,
                            checkbox_height=20, corner_radius=6, fg_color=ACCENT,
                            hover_color=ACCENT_HOVER,
                            border_color=("#3a4160" if ok else "#2a2f45"),
                            state=("normal" if ok else "disabled")
                            ).pack(side="left", padx=(4, 10))
            col = TEXT if ok else SUBTLE
            ctk.CTkLabel(row, text=label + ("" if ok else "（禁用）"), text_color=col,
                         font=ctk.CTkFont(family=FONT_UI, size=12,
                                          weight=("bold" if ok else "normal"))
                         ).pack(side="left")
            ctk.CTkLabel(row, text="· " + desc, text_color=SUBTLE,
                         font=ctk.CTkFont(family=FONT_UI, size=10)
                         ).pack(side="left", padx=(6, 0))
            vars_[key] = (v, ok)

        btns = ctk.CTkFrame(w, fg_color="transparent")
        btns.pack(fill="x", padx=14, pady=(0, 12))

        def preset(defs):
            for k, (v, ok) in vars_.items():
                v.set(ok and k in defs)

        def apply_():
            self.sel_passes = [k for k, (v, ok) in vars_.items() if ok and v.get()]
            self.cfg["passes"] = list(self.sel_passes)
            save_cfg(self.cfg)
            self.lbl_pass.configure(text=self._pass_text())
            self.logline("混淆选项已更新：%d 项（%s）"
                         % (len(self.sel_passes), ", ".join(self.sel_passes)))
            w.destroy()

        for txt, fn, fg, hv in (("标准（安全项全开）", lambda: preset(DEFAULT_PASSES), CARD_ALT, "#2b3354"),
                                ("轻量（4 项）", lambda: preset({"string_encryption", "substitution", "bcf", "split_blocks"}), CARD_ALT, "#2b3354"),
                                ("应用", apply_, ACCENT, ACCENT_HOVER)):
            b = ctk.CTkButton(btns, text=txt, height=34, corner_radius=10, fg_color=fg,
                              hover_color=hv, text_color=TEXT,
                              font=ctk.CTkFont(family=FONT_UI, size=12), command=fn)
            b.pack(side="left", padx=(0, 8), expand=True, fill="x")

    # ---------------- 源码扫描 ----------------
    def pick_dir(self):
        d = filedialog.askdirectory(title="选择源码目录")
        if d:
            self.var_src.set(d)
            base = os.path.basename(os.path.normpath(d))
            self.var_name.set(base or "myproj")
            self.scan()

    def scan(self):
        src = self.var_src.get().strip()
        if not os.path.isdir(src):
            messagebox.showerror("NKBuild", "源码目录不存在")
            return
        exts = (".c", ".cc", ".cpp", ".cxx", ".c++")
        skip = {"obj", "build", "out", "intermediate", ".git", ".gradle"}
        files, upload = [], []
        for root, dirs, fs in os.walk(src):
            dirs[:] = [d for d in dirs if d.lower() not in skip]
            for f in fs:
                p = os.path.join(root, f)
                try:
                    if os.path.getsize(p) > 100 * 1024 * 1024:
                        continue
                except OSError:
                    continue
                rel = os.path.relpath(p, src).replace("\\", "/")
                low = f.lower()
                # 全部非源码文件也上传：头文件/字体/资源缺一个都编不过
                if low.endswith((".o", ".a", ".so", ".dll", ".exe", ".zip", ".apk")):
                    continue
                upload.append(rel)
                if low.endswith(exts) and low not in ("defense.c", "defense.cpp"):
                    files.append(rel)
        self._files = files
        self._upload = upload
        self.logline("扫描到 %d 个编译单元、%d 个待上传文件（含 .h/.hpp/资源）"
                     % (len(files), len(upload)))
        self.logline("编译单元：%s" % ", ".join(files[:8]))

    # ---------------- 终止 ----------------
    def abort(self):
        if not self._busy:
            return
        self._cancel.set()
        self.logline("⛔ 已请求终止…")
        rid = self._run_id
        if rid:
            def _do():
                try:
                    Github(self.cfg.get("token", "")).cancel_run(
                        self.cfg["owner"], self.cfg["repo"], rid)
                    self.logline("已向 GitHub 发送取消请求（run %d）" % rid)
                except Exception as e:
                    self.logline("取消请求失败：%s" % e)
            threading.Thread(target=_do, daemon=True).start()
        self.btn_abort.configure(state="disabled", text="终止中…")

    # ---------------- ETA ----------------
    @staticmethod
    def estimate_seconds(npass, mb):
        return max(120, 120 + npass * 20 + min(900, int(mb * 20)))

    @staticmethod
    def fmt_dur(sec):
        sec = max(0, int(sec))
        mm, ss = divmod(sec, 60)
        hh, mm = divmod(mm, 60)
        return ("%d:%02d:%02d" % (hh, mm, ss)) if hh else ("%d:%02d" % (mm, ss))

    # ---------------- 开始 ----------------
    def start(self):
        if self.worker and self.worker.is_alive():
            messagebox.showwarning("NKBuild", "正在编译，先等这次结束")
            return
        if not self.lic_ok():
            messagebox.showerror("NKBuild", "先登录卡密（顶部授权栏）")
            self.lic_login()
            return
        token = (self.cfg.get("token") or "").strip()
        if not token:
            messagebox.showerror("NKBuild", "先在「设置」里填 GitHub Token")
            return
        src = self.var_src.get().strip()
        if not os.path.isdir(src):
            messagebox.showerror("NKBuild", "源码目录不存在")
            return
        if not getattr(self, "_files", None):
            self.scan()
        files = getattr(self, "_files", []) or []
        upload = getattr(self, "_upload", None) or files
        if not files:
            messagebox.showerror("NKBuild", "没有可编译的 .c/.cpp")
            return
        name = (self.var_name.get().strip() or "myproj").replace("/", "_")
        out_name = self.var_out.get().strip() or "lucifer"
        adb = bool(self.var_ad.get())
        self._cancel.clear()
        self._run_id = None
        mb = sum(os.path.getsize(os.path.join(src, f)) for f in upload
                 if os.path.isfile(os.path.join(src, f))) / 1048576.0
        self._eta_total = self.estimate_seconds(len(self.sel_passes), mb)
        self.logline("预计耗时约 %s（%d 项 pass，源码 %.2f MB）"
                     % (self.fmt_dur(self._eta_total), len(self.sel_passes), mb))
        self.btn_go.configure(state="disabled", text="编译中…")
        self.btn_abort.configure(state="normal", text="终止")
        self.set_prog(0)
        self.worker = threading.Thread(
            target=self._worker,
            args=(token, src, files, upload, name, out_name, adb, list(self.sel_passes)),
            daemon=True)
        self.worker.start()

    # ---------------- 工作线程 ----------------
    def _worker(self, token, src, files, upload, name, out_name, adb, sel):
        try:
            gh = Github(token)
            o, r = self.cfg["owner"], self.cfg["repo"]
            br, wf = self.cfg["branch"], self.cfg["workflow"]
            src_dir = "%s/%s" % (self.cfg.get("src_root", "projects"), name)

            self.status("校验中")
            me = gh.user()
            self.logline("登录为 %s" % me.get("login"))
            self.set_prog(5)

            self.status("上传中")
            head_sha, tree_sha = gh.head(o, r, br)
            entries, total = [], 0
            for rel in upload:
                if self._cancel.is_set():
                    raise RuntimeError("已手动终止（上传阶段）")
                p = os.path.join(src, rel)
                data = open(p, "rb").read()
                entries.append({"path": "%s/%s" % (src_dir, rel), "mode": "100644",
                                "type": "blob", "sha": gh.blob(o, r, data)})
                total += len(data)
                self.logline("  + %s (%d B)" % (rel, len(data)))
            nt = gh.tree(o, r, tree_sha, entries)
            nc = gh.commit(o, r, "NKBuild: %s (%d files)" % (name, len(entries)),
                           nt, [head_sha])
            gh.ref(o, r, br, nc)
            self.logline("已提交 %s，共 %.1f KB" % (nc[:8], total / 1024.0))
            self.set_prog(20)

            # 生成并上传 TOML（反调试跟随 ③）
            use = [k for k in sel if k != "anti_debugging"]
            if adb:
                use.append("anti_debugging")
            cfg_path = "ci/ui-%s.toml" % name
            gh.put_file(o, r, cfg_path, build_toml(use),
                        "NKBuild: ui config %s" % name)
            self.logline("已上传配置 %s（%d 项 pass）" % (cfg_path, len(use)))
            self.set_prog(30)

            inputs = {"src_dir": src_dir, "src_files": " ".join(files),
                      "presets": "none", "defense_level": "0",
                      "config_file": cfg_path, "cfg_only": cfg_path,
                      "out_name": out_name, "skip_smoke": "true",
                      "anti_debug": ("true" if adb else "false")}
            self.status("触发中")
            before = gh.latest(o, r, wf)
            before_id = before["id"] if before else 0
            gh.dispatch(o, r, wf, br, inputs)
            self.logline("已触发 配置=%s  反调试=%s" % (cfg_path, adb))

            run_id = None
            for _ in range(30):
                if self._cancel.is_set():
                    raise RuntimeError("已手动终止（还没拿到 run id）")
                time.sleep(4)
                cur = gh.latest(o, r, wf)
                if cur and cur["id"] > before_id:
                    run_id = cur["id"]
                    break
            if not run_id:
                raise RuntimeError("没等到新 run（检查 workflow 名称）")
            self._run_id = run_id
            self.logline("run id = %d（点「终止」可取消）" % run_id)

            self.status("编译中")
            printed, info = None, {}
            for i in range(240):
                if self._cancel.is_set():
                    raise RuntimeError("已手动终止（云端任务已请求取消）")
                info = gh.run(o, r, run_id) or {}
                st = info.get("status", "unknown")
                if st != printed:
                    self.logline("  状态 %s" % st)
                    printed = st
                if st == "completed":
                    break
                self.set_prog(30 + min(50, i * 2))
                time.sleep(10)

            concl = info.get("conclusion")
            if concl != "success":
                if concl is None:
                    raise RuntimeError("等太久还没跑完，去 Actions 页面看看")
                failed = []
                try:
                    for jb in gh.jobs(o, r, run_id):
                        for stp in jb.get("steps", []):
                            if stp.get("conclusion") == "failure":
                                failed.append(stp.get("name", "?"))
                except Exception:
                    pass
                msg = "编译失败：%s" % concl
                if failed:
                    msg += "\n失败步骤：" + " | ".join(failed[:4])
                if info.get("html_url"):
                    msg += "\n日志：" + info["html_url"]
                raise RuntimeError(msg)

            self.status("下载中")
            self.set_prog(85)
            arts = gh.arts(o, r, run_id)
            if not arts:
                raise RuntimeError("没有产物（artifact）")
            blob = gh.download_art(o, r, arts[0]["id"])
            import io as _io
            z = zipfile.ZipFile(_io.BytesIO(blob))
            out_dir = self._pick_out_dir(out_name)
            for n in z.namelist():
                dst = os.path.join(out_dir, os.path.basename(n))
                with open(dst, "wb") as f:
                    f.write(z.read(n))
                self.logline("  下载 %s -> %s" % (n, dst))
            self._last_out = out_dir
            self.cfg["last_out"] = out_dir
            save_cfg(self.cfg)
            self.set_prog(100)
            self.status("完成")
            self.logline("✔ 完成，产物在：%s" % out_dir)
        except Exception as e:
            self.status("失败")
            self.logline("!! %s" % e)
        finally:
            self.q.put(("done", None))

    def _pick_out_dir(self, out_name):
        cands = [
            os.path.join(os.path.expanduser("~"), "Desktop", "NKBuild-out", out_name),
            os.path.join(os.path.expanduser("~"), "Documents", "NKBuild-out", out_name),
            os.path.join(os.path.expanduser("~"), "NKBuild-out", out_name),
            os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")),
                         "NKBuild-out", out_name),
            os.path.join(os.environ.get("TEMP", os.path.expanduser("~")),
                         "NKBuild-out", out_name),
        ]
        for d in cands:
            try:
                os.makedirs(d, exist_ok=True)
                probe = os.path.join(d, ".w")
                open(probe, "w").close()
                os.remove(probe)
                return d
            except Exception:
                continue
        return os.path.expanduser("~")

    def open_out(self):
        d = self._last_out or self.cfg.get("last_out")
        if d and os.path.isdir(d):
            os.startfile(d)
        else:
            messagebox.showinfo("NKBuild", "还没有产物目录")

    # ---------------- 队列/动画 ----------------
    def _drain(self):
        try:
            while True:
                kind, val = self.q.get_nowait()
                if kind == "log":
                    self.log.insert("end", val + "\n")
                    self.log.see("end")
                elif kind == "st":
                    self._status_text = val
                    if val in ("校验中", "上传中", "触发中", "编译中", "下载中"):
                        if not self._t0:
                            self._t0 = time.time()
                        self._busy = True
                    else:
                        if self._t0:
                            self.q.put(("log", "⏱ 总用时 %s"
                                        % self.fmt_dur(time.time() - self._t0)))
                            self._t0 = None
                        self._busy = False
                        self._prog_target = 1.0 if val == "完成" else self._prog_target
                    color = GREEN if val == "完成" else (RED if val == "失败" else ACCENT)
                    self.dot.configure(text="● " + val, text_color=color)
                    if val in ("完成", "失败"):
                        self._eta_total = 0
                elif kind == "pg":
                    self._prog_target = val
                elif kind == "lic":
                    self.btn_login.configure(state="normal", text="登录")
                    ok, text, km = val[0], val[1], val[2]
                    if ok:
                        state = val[4] if len(val) > 4 else "ok"
                        self._lic_apply(state, text)
                        self._lic_km = km
                        self.cfg["kami"] = km
                        save_cfg(self.cfg)
                        self.logline("授权：%s" % text)
                    else:
                        self._lic_apply("expired", "未授权 · %s" % text)
                        self.logline("授权失败：%s" % text)
                elif kind == "licmsg":
                    self.logline("解绑：%s" % val)
                    if val == "解绑完成":
                        self._lic_apply("none", "已解绑")
                        self.cfg["kami"] = ""
                        save_cfg(self.cfg)
                elif kind == "done":
                    self._busy = False
                    self.btn_go.configure(state="normal", text="开始编译")
                    self.btn_abort.configure(state="disabled", text="终止")
                    self._cancel.clear()
                    self._run_id = None
        except queue.Empty:
            pass
        self.after(100, self._drain)

    def _tick(self):
        # 进度缓动
        self._prog += (self._prog_target - self._prog) * 0.18
        self.pbar.set(self._prog)
        self.lbl_pct.configure(text="%d%%" % int(self._prog * 100))
        # 时间 / ETA
        if self._busy and self._t0:
            sec = time.time() - self._t0
            left = (self._eta_total - sec) if self._eta_total else 0
            if self._eta_total:
                tail = ("剩 " + self.fmt_dur(left)) if left >= 0 else ("超时 " + self.fmt_dur(-left))
                txt = "◉ %s %s · %s" % (self._status_text or "进行中",
                                        self.fmt_dur(sec), tail)
            else:
                txt = "◉ %s %s" % (self._status_text or "进行中", self.fmt_dur(sec))
            self.dot.configure(text=txt, text_color=ACCENT)
        self.after(250, self._tick)


def main():
    if "--selftest" in sys.argv:
        os.makedirs(CFG_DIR, exist_ok=True)
        with open(os.path.join(CFG_DIR, "selftest.txt"), "w", encoding="utf-8") as f:
            f.write("NKBuild v6 selftest\n")
            f.write("customtkinter: %s\n" % (getattr(ctk, "__version__", "?")))
            f.write("pass 表: %d 项，默认 %d 项\n" % (len(PASS_SPEC), len(DEFAULT_PASSES)))
            f.write("配置: %s\n" % CFG_FILE)
            f.write("OK\n")
        print("selftest ok ->", os.path.join(CFG_DIR, "selftest.txt"))
        return
    if not (load_cfg().get("token") or "").strip() or "--setup" in sys.argv:
        setup_wizard()
    app = App()
    app.mainloop()


if __name__ == "__main__":
    main()
