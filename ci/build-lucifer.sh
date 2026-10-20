#!/usr/bin/env bash
# 用自建 clang + Ensia 插件把项目交叉编译成 aarch64（Android）
# 用法: CLANG=/path/clang++ bash ci/build-lucifer.sh <NDK_DIR> <PLUGIN> <OUT>
set -uo pipefail
NDK_DIR="${1:?NDK_DIR}"
PLUGIN="${2:?PLUGIN}"
OUT="${3:-lucifer}"

TC="$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64"
SYS="$TC/sysroot"
LLD="$TC/bin/ld.lld"
RT="$TC/lib/clang/21/lib/linux/aarch64"
LIBDIR="$SYS/usr/lib/aarch64-linux-android"
CLANG="${CLANG:-$PWD/llvm-build/bin/clang++}"

[ -x "$CLANG" ] || { echo "找不到 clang: $CLANG"; exit 1; }
[ -f "$PLUGIN" ] || { echo "找不到插件: $PLUGIN"; exit 1; }

# 动态 libLLVM：运行时必须能找到
export LD_LIBRARY_PATH="$(cd "$(dirname "$CLANG")/../lib" && pwd):${LD_LIBRARY_PATH:-}"
echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"

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

try_build() {
  local preset="$1" cfg="$2" out="$3"
  echo "== 尝试 preset=${preset:-<无插件>}"
  unset ENSIA
  if [ -n "$cfg" ]; then
    export ENSIA=1 ENSIA_PRESET="$preset" ENSIA_CONFIG="$cfg"
    "$CLANG" "${BASE[@]}" "-fpass-plugin=$PLUGIN" "${LIBS[@]}" \
      -o "$out" "${SRCS[@]}" -lEGL -lGLESv2 -lGLESv3 -landroid -llog 2>&1 | tail -40
  else
    "$CLANG" "${BASE[@]}" "${LIBS[@]}" \
      -o "$out" "${SRCS[@]}" -lEGL -lGLESv2 -lGLESv3 -landroid -llog 2>&1 | tail -40
  fi
  [ -f "$out" ] && { echo "成功: $out ($(stat -c%s "$out") 字节)"; return 0; }
  return 1
}

try_build max "$PWD/ensia/config/presets/max-all.toml" "$OUT.max" && { mv "$OUT.max" "$OUT"; exit 0; }
try_build mid "$PWD/ensia/config/presets/mid.toml"      "$OUT.mid" && { mv "$OUT.mid" "$OUT"; exit 0; }
echo "== 插件路线失败，退回纯加固"
try_build "" "" "$OUT.plain" && { mv "$OUT.plain" "$OUT"; exit 0; }

echo "全部失败"
exit 1
