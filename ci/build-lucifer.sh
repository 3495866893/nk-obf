#!/usr/bin/env bash
# 通用：用自建 clang + Ensia 插件把任意项目交叉编译成 aarch64（Android）
# 环境变量（都可省）：
#   CLANG        clang++ 路径
#   SRC_DIR      源码目录，默认 proj
#   SRC_FILES    源文件（相对 SRC_DIR，空格分隔），默认 "mian.cpp defense.c"
#   OUT_NAME     产物名，默认 lucifer
#   DEFENSE_LVL  defense.c 的 DEFENSE_LEVEL，默认 3（0 表示不加）
#   PRESETS      尝试顺序，默认 "max mid"（都失败则退纯加固）
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
BRAND="$PWD/ci/brand.txt"

[ -x "$CLANG" ] || { echo "找不到 clang: $CLANG"; exit 1; }

export LD_LIBRARY_PATH="$(cd "$(dirname "$CLANG")/../lib" && pwd):${LD_LIBRARY_PATH:-}"

SRCS=()
for f in $SRC_FILES; do
  if [ -f "$SRC_DIR/$f" ]; then
    SRCS+=("$SRC_DIR/$f")
  else
    echo "警告: 缺源文件 $SRC_DIR/$f"
  fi
done
[ ${#SRCS[@]} -gt 0 ] || { echo "没有源文件（SRC_FILES=$SRC_FILES）"; exit 1; }
echo "源文件: ${SRCS[*]}"

# 插件候选
PLUGINS=()
if [ -n "$PLUGIN_ARG" ] && [ -d "$PLUGIN_ARG" ]; then
  for c in "$PLUGIN_ARG/libEnsia.so" "$PLUGIN_ARG/libEnsia-linked.so"; do
    [ -f "$c" ] && PLUGINS+=("$c")
  done
elif [ -n "$PLUGIN_ARG" ] && [ -f "$PLUGIN_ARG" ]; then
  PLUGINS+=("$PLUGIN_ARG")
fi
[ ${#PLUGINS[@]} -gt 0 ] || PLUGINS=("")

BASE=(
  "--target=aarch64-linux-android21" "--sysroot=$SYS"
  "-O2" "-fPIE" "-fstack-protector-strong" "-D_FORTIFY_SOURCE=2"
  "-fvisibility=hidden" "-ffunction-sections" "-fdata-sections" "-fno-ident"
  "-nostdlib++" "--ld-path=$LLD"
  "-Wl,--gc-sections" "-Wl,--exclude-libs,ALL"
  "-Wl,-z,relro" "-Wl,-z,now" "-Wl,-z,noexecstack"
  "-Wl,--build-id=none" "-Wl,--strip-all"
)
if [ "$DEFENSE_LVL" != "0" ]; then
  BASE+=("-DDEFENSE_LEVEL=$DEFENSE_LVL" "-DDEFENSE_INTERVAL_MS=2000")
fi
LIBS=(
  -Wl,--start-group
  "$LIBDIR/libc++_static.a" "$LIBDIR/libc++abi.a" "$RT/libunwind.a"
  -lc -lm -ldl
  -Wl,--end-group
)

brand() {
  local f="$1"
  [ -f "$f" ] || return 0
  [ -x "$OBJCOPY" ] || return 0
  [ -f "$BRAND" ] || return 0
  if "$OBJCOPY" --update-section ".comment=$BRAND" "$f" 2>/dev/null; then
    echo "已写入 NK 标识: $f"
  elif "$OBJCOPY" --add-section ".comment=$BRAND" \
        --set-section-flags ".comment=merge,strings,readonly" "$f" 2>/dev/null; then
    echo "已追加 NK 标识: $f"
  fi
}

run_one() {  # $1=preset(空=不加载插件) $2=cfg $3=plugin $4=out
  local preset="$1" cfg="$2" plugin="$3" out="$4"
  echo "== 尝试 preset=${preset:-plain}"
  local extra=()
  unset ENSIA
  if [ -n "$plugin" ] && [ -n "$cfg" ] && [ -f "$cfg" ]; then
    export ENSIA=1 ENSIA_PRESET="$preset" ENSIA_CONFIG="$cfg"
    extra=("-fpass-plugin=$plugin")
  fi
  timeout 1800 "$CLANG" "${BASE[@]}" "${extra[@]}" "${LIBS[@]}" \
    -o "$out" "${SRCS[@]}" -lEGL -lGLESv2 -lGLESv3 -landroid -llog 2>&1 | tail -40
  echo "退出码: ${PIPESTATUS[0]:-?}"
  [ -f "$out" ] && { brand "$out"; echo "成功: $out ($(stat -c%s "$out") 字节)"; return 0; }
  return 1
}

if [ "${PRESETS:-max mid}" = "none" ]; then
  echo "== PRESETS=none：不加载混淆插件"
  run_one "" "" "" "$OUT.plain" && { mv "$OUT.plain" "$OUT"; exit 0; }
  echo "纯加固构建失败"; exit 1
fi

for plug in "${PLUGINS[@]}"; do
  [ -n "$plug" ] || continue
  for p in ${PRESETS:-max mid}; do
    cfg="$PWD/ensia/config/presets/$p.toml"
    [ -f "$cfg" ] || cfg="$PWD/ensia/config/presets/max-all.toml"
    run_one "$p" "$cfg" "$plug" "$OUT.$p" && { mv "$OUT.$p" "$OUT"; exit 0; }
  done
done

echo "== 插件路线失败，退回纯加固"
run_one "" "" "" "$OUT.plain" && { mv "$OUT.plain" "$OUT"; exit 0; }
echo "全部失败"
exit 1
