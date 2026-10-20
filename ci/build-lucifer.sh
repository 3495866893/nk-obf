#!/usr/bin/env bash
# 用自建 clang + Ensia 插件把项目交叉编译成 aarch64（Android），产物写入 NK 标识
# 用法: CLANG=/path/clang++ bash ci/build-lucifer.sh <NDK_DIR> <PLUGIN_DIR或.so> <OUT>
set -uo pipefail
NDK_DIR="${1:?NDK_DIR}"
PLUGIN_ARG="${2:?PLUGIN}"
OUT="${3:-lucifer}"

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
echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"

if [ -d "$PLUGIN_ARG" ]; then
  CANDIDATES=("$PLUGIN_ARG/libEnsia.so" "$PLUGIN_ARG/libEnsia-linked.so")
else
  CANDIDATES=("$PLUGIN_ARG")
fi
PLUGINS=()
for c in "${CANDIDATES[@]}"; do [ -f "$c" ] && PLUGINS+=("$c"); done
[ ${#PLUGINS[@]} -gt 0 ] || { echo "没有可用插件"; PLUGINS=(""); }

BASE=(
  "--target=aarch64-linux-android21" "--sysroot=$SYS"
  "-O2" "-fPIE" "-fstack-protector-strong" "-D_FORTIFY_SOURCE=2"
  "-fvisibility=hidden" "-ffunction-sections" "-fdata-sections" "-fno-ident"
  "-DDEFENSE_LEVEL=3" "-DDEFENSE_INTERVAL_MS=2000"
  "-nostdlib++" "--ld-path=$LLD"
  "-Wl,--gc-sections" "-Wl,--exclude-libs,ALL"
  "-Wl,-z,relro" "-Wl,-z,now" "-Wl,-z,noexecstack"
  "-Wl,--build-id=none" "-Wl,--strip-all"
)
LIBS=(
  -Wl,--start-group
  "$LIBDIR/libc++_static.a" "$LIBDIR/libc++abi.a" "$RT/libunwind.a"
  -lc -lm -ldl
  -Wl,--end-group
)
SRCS=(proj/mian.cpp proj/defense.c)

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

run_one() {  # $1=preset $2=cfg $3=plugin(可空) $4=out
  local preset="$1" cfg="$2" plugin="$3" out="$4"
  echo "== 尝试 preset=${preset:-plain} plugin=${plugin:-<none>}"
  local extra=()
  unset ENSIA
  if [ -n "$plugin" ] && [ -n "$cfg" ]; then
    export ENSIA=1 ENSIA_PRESET="$preset" ENSIA_CONFIG="$cfg"
    extra=("-fpass-plugin=$plugin")
  fi
  timeout 1200 "$CLANG" "${BASE[@]}" "${extra[@]}" "${LIBS[@]}" \
    -o "$out" "${SRCS[@]}" -lEGL -lGLESv2 -lGLESv3 -landroid -llog 2>&1 | tail -40
  local rc=${PIPESTATUS[0]:-$?}
  echo "退出码: $rc"
  [ -f "$out" ] && { brand "$out"; echo "成功: $out ($(stat -c%s "$out") 字节)"; return 0; }
  return 1
}

for plug in "${PLUGINS[@]}"; do
  [ -n "$plug" ] || continue
  run_one max "${ENV_CFG_MAX:-$PWD/ensia/config/presets/max-all.toml}" "$plug" "$OUT.max" \
    && { mv "$OUT.max" "$OUT"; exit 0; }
  run_one mid "${ENV_CFG_MID:-$PWD/ensia/config/presets/mid.toml}" "$plug" "$OUT.mid" \
    && { mv "$OUT.mid" "$OUT"; exit 0; }
done

echo "== 插件路线全失败，退回纯加固"
run_one "" "" "" "$OUT.plain" && { mv "$OUT.plain" "$OUT"; exit 0; }
echo "全部失败"
exit 1
