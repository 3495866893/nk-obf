# -*- coding: utf-8 -*-
"""论文风格横向流水线示意图生成器（中 / 英共用同一套几何布局）。

用法：
    python flow_zh.py            # 出中文图 flow_zh.png
    python flow_zh.py --lang en  # 出英文图 flow_en.png

只依赖 matplotlib + Pillow；输出到脚本自身所在目录，不假设任何其它环境。
"""

import os
import sys
import warnings

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch
from matplotlib.font_manager import FontProperties
from matplotlib import ft2font

OUT_DIR = os.path.dirname(os.path.abspath(__file__))

# ---- 字体（绝对路径，Windows 自带）----------------------------------------
FONT_ZH = r"C:\Windows\Fonts\msyh.ttc"        # 微软雅黑
FONT_ZH_BOLD = r"C:\Windows\Fonts\msyhbd.ttc"  # 微软雅黑 Bold
FONT_EN = "DejaVu Sans"

# ---- 画布与几何（zh/en 完全一致，唯一差异是文字）--------------------------
W, H = 14.0, 4.2
DPI = 300
MARGIN = 0.35
GAP = 0.34
NODE_TOP, NODE_BOT = 3.90, 1.85
AUX_TOP, AUX_BOT = 1.45, 0.65
ROUND = 0.13
NODE_LW = 1.6
AUX_LW = 1.2

FS_TITLE, FS_BODY, FS_AUX = 13.0, 9.5, 8.5
LINE_GAP = 0.27

# ---- 配色 ----------------------------------------------------------------
GREEN = ("#CDE8CE", "#3C7A4A")
BLUE = ("#CFE0F3", "#254E86")
PINK = ("#F7D3D3", "#9E3B3B")
YELLOW = ("#FBF3C9", "#8A7B22")
GRAY = ("#EDEDED", "#7A7A7A")
ARROW = "#7A7A7A"
AUX_FILL, AUX_EDGE = "#F8F8F8", "#7A7A7A"
AUX_TEXT = "#4A4A4A"

NODE_COLORS = [GREEN, BLUE, PINK, YELLOW, GRAY]

LANG = "zh"  # 由 build(lang) 设置；供 add_fit_text 选字体用

TEXT = {
    "zh": {
        "nodes": [
            ("源码", "选目录\n扫描编译单元 / 头文件 / 资源\n预检：标出危险文件"),
            ("上传", "blobs → tree → commit\n生成 ci/ui-<proj>.toml"),
            ("触发", "workflow_dispatch\n→ GitHub Actions\n缓存命中 LLVM 21\n插件 / NDK r29"),
            ("混淆编译", "逐文件 clang -c\n小 TU：8 pass\n大 TU：跳过 flattening\n链接 nk_ad.o"),
            ("产物", "<out>.cfg\n→ Release nk-builds\n→ Download/NKBuild\n一键安装"),
        ],
        "aux": [
            ("nk_ad.c 运行时看门狗", "TracerPid / maps / JNI / perf / 哈希"),
            ("断点续接 · 任务历史", ""),
            ("检查更新（云端版本号）", ""),
        ],
        "out": "flow_zh.png",
    },
    "en": {
        "nodes": [
            ("Source", "pick directory\nscan TUs, headers\nand resources\npre-check: unsafe files"),
            ("Upload", "blobs → tree → commit\nemit ci/ui-<proj>.toml"),
            ("Trigger", "workflow_dispatch\n→ GitHub Actions\ncache hit: LLVM 21\nplugins / NDK r29"),
            ("Obfuscate & Compile", "per-file clang -c\nsmall TU: 8 passes\nlarge TU: skip flattening\nlink nk_ad.o"),
            ("Artifact", "<out>.cfg\n→ Release nk-builds\n→ Download/NKBuild\none-click install"),
        ],
        "aux": [
            ("nk_ad.c runtime watchdog", "TracerPid / maps / JNI / perf / hash"),
            ("Resume · Task history", ""),
            ("Check updates (cloud version)", ""),
        ],
        "out": "flow_en.png",
    },
}


def fontprops(lang, size, bold=False):
    if lang == "zh":
        path = FONT_ZH_BOLD if (bold and os.path.exists(FONT_ZH_BOLD)) else FONT_ZH
        return FontProperties(fname=path, size=size)
    return FontProperties(family=FONT_EN, size=size, weight=("bold" if bold else "normal"))


def rounded(ax, x, y, w, h, fill, edge, lw, r=ROUND):
    ax.add_patch(
        FancyBboxPatch(
            (x, y),
            w,
            h,
            boxstyle="round,pad=0,rounding_size=%.3f" % r,
            linewidth=lw,
            edgecolor=edge,
            facecolor=fill,
            joinstyle="round",
            mutation_aspect=1.0,
            zorder=2,
        )
    )


def add_fit_text(ax, fig, x, y, s, size, bold, color, max_w_in, ha="center", va="center",
                 zorder=3):
    """写字；若单行宽度超过 max_w_in，就按 0.25pt 步进缩字号到刚好放下。"""
    size = float(size)
    while True:
        fp = fontprops(LANG, size, bold=bold)
        t = ax.text(x, y, s, ha=ha, va=va, fontproperties=fp, color=color, zorder=zorder)
        fig.canvas.draw()
        bb = t.get_window_extent(renderer=fig.canvas.get_renderer())
        w_in = bb.width / fig.dpi
        if w_in <= max_w_in or size <= 8.0:
            return t
        t.remove()
        size -= 0.25


def build(lang):
    global LANG
    LANG = lang
    spec = TEXT[lang]
    plt.rcParams.update(
        {
            "pdf.fonttype": 42,
            "ps.fonttype": 42,
            "savefig.dpi": DPI,
            "figure.facecolor": "white",
            "savefig.facecolor": "white",
        }
    )

    fig = plt.figure(figsize=(W, H), dpi=DPI)
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, W)
    ax.set_ylim(0, H)
    ax.set_axis_off()
    for sp in ax.spines.values():
        sp.set_visible(False)

    usable = W - 2 * MARGIN
    nw = (usable - GAP * 4) / 5.0
    nh = NODE_TOP - NODE_BOT
    aw = (usable - GAP * 2) / 3.0
    ah = AUX_TOP - AUX_BOT

    node_x = [MARGIN + i * (nw + GAP) for i in range(5)]
    aux_x = [MARGIN + i * (aw + GAP) for i in range(3)]
    cy = (NODE_TOP + NODE_BOT) / 2.0

    fp_t = fontprops(lang, FS_TITLE, bold=True)
    fp_b = fontprops(lang, FS_BODY)
    fp_a1 = fontprops(lang, FS_AUX, bold=True)
    fp_a2 = fontprops(lang, FS_AUX)

    # 主节点
    inner_w = nw - 0.40
    for i, (title, body) in enumerate(spec["nodes"]):
        fill, edge = NODE_COLORS[i]
        x = node_x[i]
        rounded(ax, x, NODE_BOT, nw, nh, fill, edge, NODE_LW)

        add_fit_text(
            ax,
            fig,
            x + nw / 2.0,
            NODE_TOP - 0.30,
            title,
            FS_TITLE,
            True,
            edge,
            inner_w,
        )
        # 标题下的细分隔线
        ax.plot(
            [x + 0.22, x + nw - 0.22],
            [NODE_TOP - 0.52, NODE_TOP - 0.52],
            color=edge,
            lw=0.8,
            alpha=0.55,
            zorder=3,
        )
        # 正文块在标题下方区域内垂直居中
        n_lines = body.count("\n") + 1
        line_h = 1.55 * FS_BODY / 72.0
        block_h = (n_lines - 1) * line_h
        body_top = (NODE_TOP - 0.60 + NODE_BOT + 0.16) / 2.0 + block_h / 2.0
        ax.text(
            x + 0.17,
            body_top,
            body,
            ha="left",
            va="top",
            fontproperties=fp_b,
            color="#2B2B2B",
            linespacing=1.55,
            zorder=3,
        )

    # 箭头
    for i in range(4):
        x0 = node_x[i] + nw
        x1 = node_x[i + 1]
        ax.annotate(
            "",
            xy=(x1 - 0.02, cy),
            xytext=(x0 + 0.02, cy),
            arrowprops=dict(
                arrowstyle="-|>",
                color=ARROW,
                lw=1.4,
                shrinkA=0.0,
                shrinkB=0.0,
                mutation_scale=13,
            ),
            zorder=4,
        )

    # 辅助信息带
    aux_inner = aw - 0.30
    for i, (line1, line2) in enumerate(spec["aux"]):
        x = aux_x[i]
        rounded(ax, x, AUX_BOT, aw, ah, AUX_FILL, AUX_EDGE, AUX_LW, r=0.09)
        if line2:
            add_fit_text(
                ax, fig, x + aw / 2.0, AUX_BOT + ah * 0.62, line1, FS_AUX, True,
                AUX_TEXT, aux_inner,
            )
            add_fit_text(
                ax, fig, x + aw / 2.0, AUX_BOT + ah * 0.28, line2, FS_AUX, False,
                "#6A6A6A", aux_inner,
            )
        else:
            add_fit_text(
                ax, fig, x + aw / 2.0, AUX_BOT + ah / 2.0, line1, FS_AUX, True,
                AUX_TEXT, aux_inner,
            )

    out_path = os.path.join(OUT_DIR, spec["out"])
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        fig.savefig(out_path, dpi=DPI, bbox_inches="tight", pad_inches=0.10)
    plt.close(fig)

    findfont = [str(w.message) for w in caught if "findfont" in str(w.message).lower()]
    return out_path, findfont


def all_text(lang):
    spec = TEXT[lang]
    chunks = []
    for t, b in spec["nodes"]:
        chunks += [t, b]
    for a1, a2 in spec["aux"]:
        chunks += [a1, a2]
    return "\n".join(chunks)


def glyph_report(lang):
    """用 FT2Font 逐字符确认字体里真有这些字形（缺字 = 方框乱码的根因）。"""
    from matplotlib.ft2font import FT2Font

    path = FONT_ZH if lang == "zh" else None
    if path is not None and not os.path.exists(path):
        return {"font": path, "exists": False, "missing": ["<font file missing>"]}
    font = FT2Font(path) if path else FT2Font(
        matplotlib.font_manager.findfont(FontProperties(family=FONT_EN))
    )
    text = all_text(lang)
    chars = sorted({c for c in text if not c.isspace()})
    missing = []
    for c in chars:
        try:
            idx = font.get_char_index(ord(c))
        except Exception:
            idx = 0
        if not idx:
            missing.append(c)
    return {"font": path or FONT_EN, "exists": True, "n_chars": len(chars), "missing": missing}


def png_info(path):
    from PIL import Image

    with Image.open(path) as im:
        size = im.size
    return size, os.path.getsize(path)


def main(argv):
    lang = os.environ.get("FLOW_LANG", "zh").strip().lower()
    if "--lang" in argv:
        i = argv.index("--lang")
        if i + 1 < len(argv):
            lang = argv[i + 1].strip().lower()
    if lang not in TEXT:
        lang = "zh"

    path, findfont = build(lang)
    size, nbytes = png_info(path)
    g = glyph_report(lang)

    print("[lang]      %s" % lang)
    print("[png]       %s" % path)
    print("[pixels]    %d x %d (%.0f dpi)" % (size[0], size[1], DPI))
    print("[bytes]     %d" % nbytes)
    print("[font]      %s exists=%s" % (g["font"], g.get("exists", True)))
    if "n_chars" in g:
        print("[glyphs]    %d unique chars checked, missing=%s" % (g["n_chars"], g["missing"] or "none"))
    else:
        print("[glyphs]    %s" % g["missing"])
    print("[findfont]  %s" % (findfont or "no warnings"))
    return 0 if not g.get("missing") and not findfont else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
