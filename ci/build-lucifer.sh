#!/usr/bin/env bash
# 通用：用自建 clang + Ensia 插件把任意项目交叉编译成 aarch64（Android）
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

# ---- 反调试强化模块（NK_AD=0 可关）----
# 注意：这里必须“不带 -fpass-plugin”单独编译成 .o 再链接。
# 若直接把 nk_ad.c 丢进 SRCS，它会被混淆 pass 处理，产生
# "Undefined temporary symbol .L_vXXXX" 的汇编错误（已实测）。
if [ "${NK_AD:-1}" != "0" ] && [ -f "$PWD/ci/nk_ad.c" ]; then
  NK_AD_OBJ="$PWD/nk_ad_extra.o"
  echo "反调试强化模块: 单独编译 ci/nk_ad.c -> nk_ad_extra.o（不经混淆）"
  "$CLANG" -x c --target=aarch64-linux-android21 --sysroot="$SYS" \
           "-resource-dir=$TC/lib/clang/21" -O2 -fPIE -fno-ident \
           -c "$PWD/ci/nk_ad.c" -o "$NK_AD_OBJ" || { echo "!! nk_ad 编译失败"; exit 1; }
  SRCS+=("$NK_AD_OBJ")
fi
echo "源文件: ${SRCS[*]}"

PLUGINS=()
if [ -n "$PLUGIN_ARG" ] && [ -d "$PLUGIN_ARG" ]; then
  for c in "$PLUGIN_ARG/libEnsia.so" "$PLUGIN_ARG/libEnsia-linked.so"; do
    [ -f "$c" ] && PLUGINS+=("$c")
  done
elif [ -n "$PLUGIN_ARG" ] && [ -f "$PLUGIN_ARG" ]; then
  PLUGINS+=("$PLUGIN_ARG")
fi
[ ${#PLUGINS[@]} -gt 0 ] || echo "提示: 没有插件，只做加固编译"

BASE=(
  "--target=aarch64-linux-android21" "--sysroot=$SYS"
  "-resource-dir=$TC/lib/clang/21"
  "-O2" "-fPIE" "-fstack-protector-strong" "-D_FORTIFY_SOURCE=2"
  "-fvisibility=hidden" "-ffunction-sections" "-fdata-sections" "-fno-ident"
  "-nostdlib++" "--ld-path=$LLD"
  "-Wl,--gc-sections" "-Wl,--exclude-libs,ALL"
  "-Wl,-z,relro" "-Wl,-z,now" "-Wl,-z,noexecstack"
  "-Wl,--build-id=none" "-Wl,--strip-all"
)
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

try() {  # $1=preset(空=不加插件) $2=cfg $3=plugin $4=out
  local preset="$1" cfg="$2" plugin="$3" out="$4"
  echo "== 尝试 preset=${preset:-plain} plugin=${plugin:-none}"
  local extra=()
  unset ENSIA
  if [ -n "$plugin" ] && [ -n "$cfg" ] && [ -f "$cfg" ]; then
    export ENSIA=1 ENSIA_CONFIG="$cfg"
    if [ "$preset" != "custom" ]; then
      export ENSIA_PRESET="${preset}"
    fi
    extra=("-fpass-plugin=$plugin")
  fi
  timeout 1800 "$CLANG" "${BASE[@]}" "${extra[@]}" "${LIBS[@]}" \
    -o "$out" "${SRCS[@]}" -lEGL -lGLESv2 -lGLESv3 -landroid -llog 2>&1 | tail -30
  if [ -f "$out" ]; then
    brand "$out"
    echo "成功: $out ($(stat -c%s "$out") 字节)"
    return 0
  fi
  return 1
}

# 只用指定 TOML（不设 preset 环境变量，TOML 里的 enabled 才是最终依据）
if [ -n "${CFG_ONLY:-}" ] && [ -f "${CFG_ONLY:-}" ]; then
  for plug in "${PLUGINS[@]:-}"; do
    [ -n "${plug:-}" ] || continue
    echo "== 只用配置: $CFG_ONLY（IR 级 pass 按 TOML 生效）"
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
      cfg="$PWD/ensia/config/presets/$p.toml"
      [ -f "$cfg" ] || cfg="$PWD/ensia/config/presets/max-all.toml"
      if try "$p" "$cfg" "$plug" "$OUT.$p"; then mv "$OUT.$p" "$OUT"; exit 0; fi
    done
  done
fi

echo "== 回退：纯加固（不加载插件）"
if try "" "" "" "$OUT.plain"; then mv "$OUT.plain" "$OUT"; exit 0; fi
echo "!! 全部失败"
exit 1
