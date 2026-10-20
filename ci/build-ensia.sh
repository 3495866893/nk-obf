#!/usr/bin/env bash
# 构建 Ensia 插件（host x86_64）。失败就真失败（不再静默通过）。
# 用法: bash ci/build-ensia.sh <src> <build> <llvm-build> [llvm-src]
set -euo pipefail
SRC="${1:-ensia/src}"
BUILD="${2:-ensia-build}"
LLVM_DIR="${3:-$PWD/llvm-build}"
LLVM_SRC="${4:-$PWD/llvm-src}"

CXX="$LLVM_DIR/bin/clang++"
[ -x "$CXX" ] || CXX=$(command -v clang++)

mkdir -p "$BUILD"
mapfile -t SRCS < <(find "$SRC/obfuscation" -name "*.cpp" ! -name "RustCompat.cpp")
echo "源文件: ${#SRCS[@]} 个"
[ ${#SRCS[@]} -gt 0 ] || { echo "!! 没找到 ensia 源码"; exit 1; }

INC=(-I"$LLVM_SRC/llvm/include" -I"$LLVM_DIR/include" -I"$SRC/obfuscation/include")
FLAGS=(-fPIC -O2 -std=c++17 -fno-rtti -fexceptions -DLLVM_NEXT=1
       -DENSIA_TARGET_LLVM_VERSION=21 -w)

echo "== 直接编译为插件（LLVM 符号留 UND，由宿主 clang 提供）"
"$CXX" "${FLAGS[@]}" -shared -Wl,-soname,libEnsia.so "${INC[@]}" \
  "${SRCS[@]}" -o "$BUILD/libEnsia.so"

[ -f "$BUILD/libEnsia.so" ] || { echo "!! libEnsia.so 没生成"; exit 1; }
ls -lh "$BUILD/libEnsia.so"

echo "== 校验导出入口"
if nm -D "$BUILD/libEnsia.so" | grep -q llvmGetPassPluginInfo; then
  echo "OK: 导出 llvmGetPassPluginInfo"
else
  echo "!! 缺少 llvmGetPassPluginInfo"
  exit 1
fi

UND=$(nm -D "$BUILD/libEnsia.so" | grep -c " U .*llvm" || true)
echo "UND llvm 符号: $UND"
[ "$UND" -gt 100 ] || echo "!! 警告：UND 符号偏少，可能误链了 LLVM 静态库"
