#!/usr/bin/env bash
# 通用：用自建 clang + Nk 插件把任意项目交叉编译成 aarch64（Android）
# 用法: bash ci/build-lucifer.sh <NDK_DIR> [PLUGIN_DIR|PLUGIN_SO] [OUT]
set -uo pipefail
NDK_DIR="${1:?NDK_DIR}"
PLUGIN_ARG="${2:-}"
OUT="${3:-${OUT_NAME:-lucifer}}"
SRC_DIR="${SRC_DIR:-proj}"
SRC_FILES="${SRC_FILES:-mian.cpp defense.c}"
DEFENSE_LVL="${DEFENSE_LVL:-3}"

TC="$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64"
SYS="$TC/sysroot"
LLD="$TC/bin/ld.lld"
RT="$TC/lib/clang/21/lib/linux/aarch64"
LIBDIR="$SYS/usr/lib/aarch64-linux-android"
CLANG="${CLANG:-$PWD/llvm-build/bin/clang++}"
OBJCOPY="$TC/bin/llvm-objcopy"
BRAND="${BRAND_FILE:-$PWD/ci/brand.txt}"

[ -x "$CLANG" ] || { echo "!! 找不到 clang: $CLANG"; exit 1; }
export LD_LIBRARY_PATH="$(cd "$(dirname "$CLANG")/../lib" && pwd):${LD_LIBRARY_PATH:-}"

SRCS=()
for f in $SRC_FILES; do
  if [ -f "$SRC_DIR/$f" ]; then SRCS+=("$SRC_DIR/$f"); else echo "警告: 缺 $SRC_DIR/$f"; fi
done
[ ${#SRCS[@]} -gt 0 ] || { echo "!! 没有源文件 (SRC_DIR=$SRC_DIR SRC_FILES=$SRC_FILES)"; exit 1; }

# ---- nk-autoincludes: 自动派生 -I 头文件搜索路径 ----
# 单文件工程（mian.cpp 那种）靠引号包含相对当前文件即可；多目录工程
# （src/ImGui/、src/Vulkan/ …）必须显式给 -I，否则 #include "imgui.h" 直接
# fatal error。这里从 SRC_DIR 全树扫一遍含头文件的目录，再补上所给源文件
# 自身的目录与它的上一级，保证 <xxx.h> 与 "sub/xxx.h" 两种写法都能命中。
INCS=()
_nk_add_inc() {
  local d="$1"
  [ -d "$d" ] || return 0
  local a
  for a in "${INCS[@]:-}"; do [ "$a" = "-I$d" ] && return 0; done
  INCS+=("-I$d")
}
_nk_add_inc "$SRC_DIR"
while IFS= read -r d; do _nk_add_inc "$d"; done < <(
  find "$SRC_DIR" -type d -not -path '*/.*' -not -path '*/build/*'        -not -path '*/obj/*' -not -path '*/.git/*' 2>/dev/null | head -80
)
for f in "${SRCS[@]}"; do
  case "$f" in *.o) continue;; esac
  _nk_add_inc "$(dirname "$f")"
  _nk_add_inc "$(dirname "$(dirname "$f")")"
done
echo "头文件路径 (${#INCS[@]}): ${INCS[*]:-无}"
# ---- /nk-autoincludes ----

# ---- 反调试强化模块 ----
# 注意：这里必须“不带 -fpass-plugin”单独编译成 .o 再链接。
# 若直接把 nk_ad.c 丢进 SRCS，它会被混淆 pass 处理，产生
# "Undefined temporary symbol .L_vXXXX" 的汇编错误（已实测）。
#
# NK_AD=0 时不再“删掉这个 .o”，而是用 -DNK_AD_OFF 编成空实现：
#   Nk 的 anti_debugging pass 会在每个函数入口注入 call __nk_ad_check()，
#   一旦关掉该 pass，实测产物在静态初始化阶段 100% CPU 死循环（0 字节输出）。
#   所以 pass 永远开着，"关闭反调试"只把实现换成空函数。
if [ -f "$PWD/ci/nk_ad.c" ]; then
  NK_AD_OBJ="$PWD/nk_ad_extra.o"
  # ---- 随机入口符号（每次构建不同，成品里搜不到固定名字）----
  NK_AD_SYM="__nk_$(head -c8 /dev/urandom | od -An -tx1 | tr -d ' \n')_g"
  export NK_AD_SYM
  echo "反调试入口符号: $NK_AD_SYM"
  # ---- 给 nk_ad.c 用的插件路径（PLUGINS 数组在后面才建，这里先探一次）----
  NK_PLUG=""
  if [ -n "$PLUGIN_ARG" ]; then
    for c in "$PLUGIN_ARG/libNk.so" "$PLUGIN_ARG/libNk-linked.so"; do
      [ -f "$c" ] && NK_PLUG="$c" && break
    done
  fi
  if [ "${NK_AD:-1}" = "0" ]; then
    NK_AD_DEF="-DNK_AD_OFF=1"
    echo "反调试强化模块: NK_AD=0 -> 编成空实现（anti_debugging pass 仍开）"
  else
    NK_AD_DEF=""
    echo "反调试强化模块: 单独编译 ci/nk_ad.c -> nk_ad_extra.o（不经混淆）"
  fi
  # nk_ad.c 自己也过一遍混淆（字符串加密 + 常量加密 + 指令替换 + 虚假控制流 +
  # 块分割 + 间接跳转 + 反调试），但用 nk-nofold 配置跳过 flattening（大 TU 上会炸内存）。
  # NK_SKIP_ADB=1 让 Pass 不给这个 TU 自己插 __nk_ad_check 调用 —— 否则自己调自己。
  # 优先用 nk-self.toml（只字符串/常量/指令替换）；bcf 与 indirect_branch
  # 会把 nk_ad.c 里内联 svc 的自省逻辑编成不可达或死循环，导致启动零输出卡死。
  NK_SELF_CFG="$PWD/nk/config/presets/nk-self.toml"
  [ -f "$NK_SELF_CFG" ] || NK_SELF_CFG="$PWD/nk/config/presets/nk-nofold.toml"
  [ -f "$NK_SELF_CFG" ] || NK_SELF_CFG="$PWD/nk/config/presets/nk-light.toml"
  NK_SELF_PLUG=()
  if [ -n "$NK_PLUG" ] && [ -f "$NK_SELF_CFG" ]; then
    NK_SELF_PLUG=("-fpass-plugin=$NK_PLUG")
    echo "反调试模块自混淆: $(basename "$NK_SELF_CFG")（跳过 flattening，不插自身检查）"
  else
    echo "反调试模块自混淆: 跳过（无插件或配置）"
  fi
  # shellcheck disable=SC2086
  NK_SKIP_ADB=1 NK=1 NK_CONFIG="$NK_SELF_CFG" \
  "$CLANG" -x c --target=aarch64-linux-android21 --sysroot="$SYS" \
           "-resource-dir=$TC/lib/clang/21" -O2 -fPIE -fno-ident \
           -fvisibility=hidden "-DNK_AD_SYM=$NK_AD_SYM" $NK_AD_DEF \
           "${NK_SELF_PLUG[@]}" \
           -c "$PWD/ci/nk_ad.c" -o "$NK_AD_OBJ" || { echo "!! nk_ad 编译失败"; exit 1; }
  SRCS+=("$NK_AD_OBJ")
fi
echo "源文件: ${SRCS[*]}"

PLUGINS=()
if [ -n "$PLUGIN_ARG" ] && [ -d "$PLUGIN_ARG" ]; then
  for c in "$PLUGIN_ARG/libNk.so" "$PLUGIN_ARG/libNk-linked.so"; do
    [ -f "$c" ] && PLUGINS+=("$c")
  done
elif [ -n "$PLUGIN_ARG" ] && [ -f "$PLUGIN_ARG" ]; then
  PLUGINS+=("$PLUGIN_ARG")
fi
[ ${#PLUGINS[@]} -gt 0 ] || echo "提示: 没有插件，只做加固编译"

BASE=(
  "--target=aarch64-linux-android21" "--sysroot=$SYS"
  "-resource-dir=$TC/lib/clang/21"
  "-O2" "-fPIE" "-fstack-protector-strong" "-D_FORTIFY_SOURCE=2" "-DVK_USE_PLATFORM_ANDROID_KHR" "-DNDEBUG" "-fno-asynchronous-unwind-tables" "-fno-unwind-tables"
  "-fvisibility=hidden" "-ffunction-sections" "-fdata-sections" "-fno-ident"
  "-nostdlib++" "--ld-path=$LLD"
  "-Wl,--no-gc-sections" "-Wl,--exclude-libs,ALL"
  "-Wl,-z,relro" "-Wl,-z,now" "-Wl,-z,noexecstack"
  "-Wl,--build-id=none" "-Wl,--strip-all"
)
BASE+=("${INCS[@]:-}")

[ "$DEFENSE_LVL" != "0" ] && BASE+=("-DDEFENSE_LEVEL=$DEFENSE_LVL" "-DDEFENSE_INTERVAL_MS=2000")
# 注意：不要把 $LIBDIR 放进 -L，否则 -lc 会优先命中静态 libc.a（缺 pthread_atfork）
#       API 目录里的 libc.so 才是正确的 stub
LIBS=(-L"$RT" -L"$SYS/usr/lib/aarch64-linux-android/21"
      -Wl,--start-group
      "$LIBDIR/libc++_static.a" "$LIBDIR/libc++abi.a" "$RT/libunwind.a"
      -lc -lm -ldl -Wl,--end-group)

brand() {
  local f="$1"
  [ -f "$f" ] || return 0
  [ -x "$OBJCOPY" ] || return 0
  [ -f "$BRAND" ] || return 0
  "$OBJCOPY" --update-section ".comment=$BRAND" "$f" 2>/dev/null \
    && { echo "已写入 NK 标识: $f"; return 0; }
  "$OBJCOPY" --add-section ".comment=$BRAND" \
    --set-section-flags ".comment=merge,strings,readonly" "$f" 2>/dev/null \
    && echo "已追加 NK 标识: $f"
}

# ---- NK_VENDOR_GUARD -------------------------------------------------------
# 有些项目把 .c 直接 #include 进头文件（典型：res/pz.h 里 #include "cJSON.c"），
# 上传时只带了 .h，clang 就死在 `fatal error: 'cJSON.c' file not found`。
# 这里在编译前逐个核对 `#include "xxx.c"`，缺的先从 ci/vendor 补，补不到就报人话。
nk_vendor_guard() {
  local srcdir="$1" vendor="$2"
  [ -d "$srcdir" ] || return 0
  local missing="" inc base dst
  while IFS= read -r inc; do
    [ -z "$inc" ] && continue
    base="$(basename "$inc")"
    if find "$srcdir" -name "$base" -print -quit 2>/dev/null | grep -q .; then
      continue
    fi
    if [ -f "$vendor/$base" ]; then
      dst="$srcdir/$inc"
      case "$inc" in /*) dst="$inc" ;; esac
      mkdir -p "$(dirname "$dst")" 2>/dev/null || true
      if cp "$vendor/$base" "$dst" 2>/dev/null; then
        echo "   [vendored] 缺 $base → 已从 ci/vendor 补到 $inc"
        continue
      fi
    fi
    missing="$missing $base"
  done < <(grep -rhoE '#[[:space:]]*include[[:space:]]*"[^"]+\.(c|cc|cpp)"' "$srcdir" 2>/dev/null \
           | sed -E 's/.*"([^"]+)".*/\1/' | sort -u)

  if [ -n "$missing" ]; then
    echo "!! 缺少被 #include 的源文件：$missing"
    echo "!! 这些 .c/.cpp 是被头文件直接 include 的（不是单独编译单元），但目录里没有；"
    echo "!! 把它们放回工程（或放进 ci/vendor/）再编译。"
    return 3
  fi
  return 0
}
# ---------------------------------------------------------------------------

try() {  # $1=preset(空=不加插件) $2=cfg $3=plugin $4=out
  local preset="$1" cfg="$2" plugin="$3" out="$4"
  echo "== 尝试 preset=${preset:-plain} plugin=${plugin:-none}"
  local extra=()
  unset NK
  if [ -n "$plugin" ] && [ -n "$cfg" ] && [ -f "$cfg" ]; then
    export NK=1 NK_CONFIG="$cfg"
    if [ "$preset" != "custom" ]; then
      export NK_PRESET="${preset}"
    fi
    extra=("-fpass-plugin=$plugin")
  fi
  rm -f "$out".s /tmp/nk_err.txt
  # 逐文件编译：一次把 17 个 TU 丢给 clang 时，一次 SIGTERM 就全线失忆。
  # 逐文件能看到停在哪一个、内存/磁盘在哪一步爆掉。
  local CF=() a
  for a in "${BASE[@]}"; do
    case "$a" in
      -Wl,*|--ld-path=*) ;;   # 连接期参数，编译期不用（否则每个文件刷一堆 unused 警告）
      *) CF+=("$a") ;;
    esac
  done
  local objs=() o n i t0 rc fopt fsz
  n=${#SRCS[@]}
  i=0
  for f in "${SRCS[@]}"; do
    # 已经是 .o 的（例如单独编译、不经混淆的 nk_ad_extra.o）直接进链接列表
    case "$f" in
      *.o|*.a) objs+=("$f"); continue ;;
    esac
    i=$((i + 1))
    o="/tmp/nk_o_$$_${i}_$(basename "${f%.*}").o"
    # 大 TU 判定不能看源文件大小：main.cpp 只有 1.3 KB，但 #include 了 pz.h/xz.h/Encrypt.h，
    # 预处理后有几 MB、几万行 —— 按源大小判断会漏判，8 个 pass 全开直接把内存吃光，
    # runner 判超限 SIGTERM（exit 143）。这里先跑 clang -E 数真正喂给前端的字节数。
    fsz=$(stat -c%s "$f" 2>/dev/null || echo 0)
    ppsz=$(timeout 300 "$CLANG" "${CF[@]}" -E -P "$f" 2>/dev/null | wc -c)
    [ -z "$ppsz" ] && ppsz=0
    # 大 TU 自动降优化级别：-O2 后端在混淆后的爆炸 IR 上内存/时间都是几倍
    fopt="${NKO:--O2}"
    if [ "$fsz" -gt 300000 ] || [ "$ppsz" -gt 1500000 ]; then fopt="-O1"; fi
    # 大 TU 换成轻量配置（只加密字符串 + 指令替换 + 反调试），
    # flattening/bcf/indirect 在这种 TU 上会把内存吃爆 → runner SIGTERM 143
    NOFOLD="$PWD/nk/config/presets/nk-nofold.toml"    # 8 pass 减 flattening
    LIGHT="$PWD/nk/config/presets/nk-light.toml"      # 兜底：极重文件才用
    if { [ "$fsz" -gt 200000 ] || [ "$ppsz" -gt 800000 ]; } && [ -f "$NOFOLD" ]; then
      export NK_CONFIG="$NOFOLD"
      echo "   → 大 TU（预处理 ${ppsz}B）：跳过 flattening，其余 7 个 pass 照常"
    elif { [ "$fsz" -gt 200000 ] || [ "$ppsz" -gt 800000 ]; } && [ -f "$LIGHT" ]; then
      export NK_CONFIG="$LIGHT"
      echo "   → 大 TU（预处理 ${ppsz}B）：退回 nk-light.toml（只 3 个 pass）"
    else
      export NK_CONFIG="$cfg"
    fi
    echo "  [$i/$n] 编译 $f（源 $(numfmt --to=iec "$fsz" 2>/dev/null || echo "$fsz")B / 预处理 $(numfmt --to=iec "$ppsz" 2>/dev/null || echo "$ppsz")B，优化级别 $fopt）"
    t0=$(date +%s)
    # shellcheck disable=SC2086
    timeout 1800 "$CLANG" "${CF[@]}" "$fopt" "${extra[@]}" ${NK_DIAG:+-save-temps=obj} \
      -c "$f" -o "$o" 2>&1 \
      | tee -a /tmp/nk_err.txt | stdbuf -oL -eL sed -u 's/^/     /'
    rc=${PIPESTATUS[0]}
    echo "     rc=$rc 耗时 $(( $(date +%s) - t0 ))s · 可用内存 $(free -m | awk '/^Mem:/{print $7}')MB · /tmp 剩余 $(df -m /tmp | awk 'NR==2{print $4}')MB"
    if [ "$rc" != "0" ] || [ ! -f "$o" ]; then
      echo "  !! 这个文件编译失败: $f"
      return 1
    fi
    objs+=("$o")
    # 每 4 个文件清一次中间产物，防止 15 MB 级头文件把磁盘吃光
    if [ $((i % 4)) = "0" ]; then rm -rf /tmp/nk_o_*.d /tmp/nk_o_*.i /tmp/nk_o_*.s; fi
  done
  echo "  [链接] ${#objs[@]} 个 .o -> $out"
  timeout 1800 "$CLANG" "${CF[@]}" "${NKO:--O2}" "--ld-path=$LLD" "-Wl,--noinhibit-exec" "${extra[@]}" "${LIBS[@]}" \
    -o "$out" "${objs[@]}" -lEGL -lGLESv2 -lGLESv3 -landroid -llog 2>&1 \
    | tee -a /tmp/nk_err.txt | tail -30
  rc=${PIPESTATUS[0]}
  rm -f "${objs[@]}" 2>/dev/null || true
  echo "  链接 rc=$rc"
  if [ -f "$out" ]; then
    brand "$out"
    echo "成功: $out ($(stat -c%s "$out") 字节)"
    return 0
  fi
  if [ -n "$plugin" ] && [ -f "$cfg" ]; then
    echo "== IR 诊断（失败后自动转储）=="
    rm -f /tmp/nk_ir.ll
    timeout 1800 "$CLANG" "${BASE[@]}" "${extra[@]}" -S -emit-llvm \
      -o /tmp/nk_ir.ll "${SRCS[@]}" >/dev/null 2>&1 || true
    if [ -f /tmp/nk_ir.ll ]; then
      echo "IR 已转储: $(wc -l < /tmp/nk_ir.ll) 行"
      echo "_v 定义数: $(grep -cE '^@_v[0-9a-f]+ = (internal|private|dso_local)' /tmp/nk_ir.ll || true)"
      echo "_v 声明数: $(grep -cE '^@_v[0-9a-f]+ = external' /tmp/nk_ir.ll || true)"
      echo "--- 声明样例 ---"
      grep -E '^@_v[0-9a-f]+ = external' /tmp/nk_ir.ll | head -8 || true
      echo "--- 定义样例 ---"
      grep -E '^@_v[0-9a-f]+ = ' /tmp/nk_ir.ll | head -8 || true
      echo "--- 引用样例 ---"
      grep -oE '@_v[0-9a-f]+' /tmp/nk_ir.ll | sort | uniq -c | sort -rn | head -8 || true
    else
      echo "IR 转储失败"
    fi
    echo "== 汇编核对（同一编译的 .s）=="
    SS=""
    for cand in "$out".s "$OUT".s "$OUT".cfg.s $(ls -t *.s 2>/dev/null | head -1); do
      [ -f "$cand" ] && { SS="$cand"; break; }
    done
    echo ".s 文件: ${SS:-无}"
    if [ -n "$SS" ]; then
      echo ".s 行数: $(wc -l < "$SS")  .L_v 标签: $(grep -cE '^\.L_v[0-9a-f]+:' "$SS" || true)  _v 标签: $(grep -cE '^_v[0-9a-f]+:' "$SS" || true)"
      echo "报错符号全集: $(grep -oE '\.?L?_v[0-9a-f]{8,}' /tmp/nk_err.txt | sort -u | tr '\n' ' ')"
      for sym in $(grep -oE '\.?L?_v[0-9a-f]{8,}' /tmp/nk_err.txt | sort -u | head -3); do
        base=$(echo "$sym" | sed 's/^\.L//')
        echo "--- $base : 命中 $(grep -c "$base" "$SS" || true) 行 ---"
        grep -m2 -n "$base" "$SS" | cut -c1-130 || true
      done
    fi
    echo "== 汇编核对(旧) =="
    rm -f /tmp/nk_s.s
    timeout 1800 "$CLANG" "${BASE[@]}" "${extra[@]}" -S -o /tmp/nk_s.s "${SRCS[@]}" >/dev/null 2>&1 || true
    if [ -f /tmp/nk_s.s ]; then
      echo ".s 行数: $(wc -l < /tmp/nk_s.s)"
      syms=$(grep -oE '_?\.?L_v[0-9a-f]{8,}' /tmp/nk_err.txt | sort -u | head -4)
      echo "报错符号: $(echo $syms | tr '\n' ' ')"
      for sym in $syms; do
        base=$(echo "$sym" | sed 's/^\.L//')
        echo "--- $base ---"
        echo "  出现次数: $(grep -c "$base" /tmp/nk_s.s || true)"
        echo "  定义标签: $(grep -cE "^\s*(\.local\s+)?${base}:|^\s*\.type\s+${base},\s*%object" /tmp/nk_s.s || true)"
        echo "  局部声明: $(grep -cE "^\s*\.local\s+${base}" /tmp/nk_s.s || true)"
        grep -m3 -n "$base" /tmp/nk_s.s | cut -c1-120 || true
      done
      echo "--- .s 中 .L_v 前缀数量 ---"
      echo ".L_v 标签: $(grep -cE '^\.L_v[0-9a-f]+:' /tmp/nk_s.s || true)   _v 标签: $(grep -cE '^_v[0-9a-f]+:' /tmp/nk_s.s || true)"
    else
      echo ".s 生成失败"
    fi
  fi
  return 1
}

# 只用指定 TOML（不设 preset 环境变量，TOML 里的 enabled 才是最终依据）
if [ -n "${CFG_ONLY:-}" ] && [ -f "${CFG_ONLY:-}" ]; then
  for plug in "${PLUGINS[@]:-}"; do
    [ -n "${plug:-}" ] || continue
    echo "== 只用配置: $CFG_ONLY（IR 级 pass 按 TOML 生效）"
nk_vendor_guard "$PWD/$SRC_DIR" "$PWD/ci/vendor"
if [ $? -ne 0 ]; then
  echo "!! 源码不完整：先补齐被 #include 的 .c 文件（见上）"
  exit 1
fi

    if try "custom" "$CFG_ONLY" "$plug" "$OUT.cfg"; then
      mv "$OUT.cfg" "$OUT"; exit 0
    fi
  done
  echo "!! 指定配置编译失败"
  exit 1
fi

# 允许用一份指定 TOML 直接跑（不看 presets）
if [ -n "${CFG_OVERRIDE:-}" ] && [ -f "${CFG_OVERRIDE:-}" ]; then
  for plug in "${PLUGINS[@]:-}"; do
    [ -n "${plug:-}" ] || continue
    echo "== 使用指定配置: $CFG_OVERRIDE"
    if try "custom" "$CFG_OVERRIDE" "$plug" "$OUT.ovr"; then
      mv "$OUT.ovr" "$OUT"; exit 0
    fi
  done
  echo "== 指定配置失败，继续走 presets"
fi

if [ "${PRESETS:-max mid}" != "none" ]; then
  for plug in "${PLUGINS[@]:-}"; do
    [ -n "${plug:-}" ] || continue
    for p in ${PRESETS:-max mid}; do
      cfg="$PWD/nk/config/presets/$p.toml"
      [ -f "$cfg" ] || cfg="$PWD/nk/config/presets/max-all.toml"
      if try "$p" "$cfg" "$plug" "$OUT.$p"; then mv "$OUT.$p" "$OUT"; exit 0; fi
    done
  done
fi

echo "== 回退：纯加固（不加载插件）"
if try "" "" "" "$OUT.plain"; then mv "$OUT.plain" "$OUT"; exit 0; fi
echo "!! 全部失败"
exit 1
