#!/usr/bin/env bash
# 构建 Ensia 插件（host x86_64），两种方式：
#   1) cmake（首选）
#   2) 失败则退回「直接编译所有 .cpp → -shared .so」（LLVM 符号留 undefined，由宿主 clang 提供）
set -uo pipefail

SRC="${1:-ensia/src}"
BUILD="${2:-ensia-build}"
LLVM_DIR="${3:-$PWD/llvm-build}"

echo "== 方式 1: cmake"
if cmake -S "$SRC" -B "$BUILD" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DLLVM_DIR="$LLVM_DIR/lib/cmake/llvm" \
      -DLLVM_NEXT=1 2>&1 | tail -40 && ninja -C "$BUILD" 2>&1 | tail -40; then
  if find "$BUILD" -name "*.so" | grep -q .; then
    echo "cmake 构建成功"
    find "$BUILD" -name "*.so" -exec ls -lh {} \;
    exit 0
  fi
fi

echo "== cmake 失败，退回直接编译"
CXX="$LLVM_DIR/bin/clang++"
[ -x "$CXX" ] || CXX=$(command -v clang++)
rm -rf "$BUILD" && mkdir -p "$BUILD"

CFLAGS=$( "$LLVM_DIR/bin/llvm-config" --cxxflags 2>/dev/null || echo "-I$PWD/llvm-src/llvm/include -I$LLVM_DIR/include -std=c++17" )
CFLAGS="$CFLAGS -fPIC -O2 -DLLVM_NEXT=1 -fexceptions"

SRCS=$(find "$SRC/obfuscation" -name "*.cpp")
echo "源文件数: $(echo "$SRCS" | wc -l)"

# shellcheck disable=SC2086
"$CXX" $CFLAGS -shared -Wl,-soname,libEnsia.so \
  -I"$SRC/obfuscation/include" -I"$SRC/compat" \
  $SRCS -o "$BUILD/libEnsia.so" 2>&1 | tail -60

ls -lh "$BUILD/libEnsia.so"
# 校验：必须导出插件入口
if command -v nm >/dev/null; then
  nm -D "$BUILD/libEnsia.so" | grep -c llvmGetPassPluginInfo || true
fi
