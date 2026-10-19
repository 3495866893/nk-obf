#!/usr/bin/env bash
# 用自建 clang + Ensia 插件把项目交叉编译成 aarch64（Android）
# 用法: bash ci/build-lucifer.sh <NDK_DIR> <PLUGIN> <OUT>
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

export ENSIA=1
export ENSIA_PRESET="${ENSIA_PRESET:-max}"
export ENSIA_CONFIG="${ENSIA_CONFIG:-$PWD/ensia/config/presets/max-all.toml}"

COMMON=(
  "--target=aarch64-linux-android21" "--sysroot=$SYS"
  "-fpass-plugin=$PLUGIN"
  "-O2" "-fPIE" "-fstack-protector-strong" "-D_FORTIFY_SOURCE=2"
  "-fvisibility=hidden" "-ffunction-sections" "-fdata-sections" "-fno-ident"
  "-DDEFENSE_LEVEL=3" "-DDEFENSE_INTERVAL_MS=2000"
  "-nostdlib++" "--ld-path=$LLD"
  "-Wl,--gc-sections" "-Wl,--exclude-libs,ALL"
  "-Wl,-z,relro" "-Wl,-z,now" "-Wl,-z,noexecstack"
  "-Wl,--build-id=none" "-Wl,--strip-all"
)

echo "== 第一次尝试（Ensia 满配）"
"$CLANG" "${COMMON[@]}" \
  -Wl,--start-group \
  "$LIBDIR/libc++_static.a" "$LIBDIR/libc++abi.a" "$RT/libunwind.a" \
  -lc -lm -ldl -Wl,--end-group \
  -o "$OUT.1" proj/mian.cpp proj/defense.c \
  -lEGL -lGLESv2 -lGLESv3 -landroid -llog 2>&1 | tail -60
[ -f "$OUT.1" ] && { mv "$OUT.1" "$OUT"; echo "OK: 满配产物"; }

if [ -f "$OUT" ]; then
  ls -l "$OUT"
  file "$OUT" || true
  exit 0
fi

echo "== 满配失败，降档 mid"
export ENSIA_PRESET=mid
export ENSIA_CONFIG="$PWD/ensia/config/presets/mid.toml"
"$CLANG" "${COMMON[@]}" \
  -Wl,--start-group \
  "$LIBDIR/libc++_static.a" "$LIBDIR/libc++abi.a" "$RT/libunwind.a" \
  -lc -lm -ldl -Wl,--end-group \
  -o "$OUT.2" proj/mian.cpp proj/defense.c \
  -lEGL -lGLESv2 -lGLESv3 -landroid -llog 2>&1 | tail -60
[ -f "$OUT.2" ] && { mv "$OUT.2" "$OUT"; echo "OK: mid 降档产物"; }

if [ -f "$OUT" ]; then
  ls -l "$OUT"
  file "$OUT" || true
  exit 0
fi

echo "== mid 也失败，关闭插件（纯加固）"
unset ENSIA
"$CLANG" "${COMMON[@]}" \
  -Wl,--start-group \
  "$LIBDIR/libc++_static.a" "$LIBDIR/libc++abi.a" "$RT/libunwind.a" \
  -lc -lm -ldl -Wl,--end-group \
  -o "$OUT.3" proj/mian.cpp proj/defense.c \
  -lEGL -lGLESv2 -lGLESv3 -landroid -llog 2>&1 | tail -60
[ -f "$OUT.3" ] && { mv "$OUT.3" "$OUT"; echo "OK: 无插件（仅加固）产物"; ls -l "$OUT"; exit 0; }

echo "全部失败"
exit 1
