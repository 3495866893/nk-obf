#!/usr/bin/env bash
# 构建 Ensia 插件（host x86_64），只编主目标 Ensia（Rust 变体针对 LLVM23，跳过）
set -uo pipefail
SRC="${1:-ensia/src}"
BUILD="${2:-ensia-build}"
LLVM_DIR="${3:-$PWD/llvm-build}"

echo "== 方式 1: cmake + ninja Ensia"
if cmake -S "$SRC" -B "$BUILD" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DLLVM_DIR="$LLVM_DIR/lib/cmake/llvm" 2>&1 | tail -20 \
   && ninja -C "$BUILD" Ensia 2>&1 | tail -60 \
   && find "$BUILD" -name "*.so" | grep -q .; then
  echo "cmake 构建成功"
  find "$BUILD" -name "*.so" -exec ls -lh {} \;
  exit 0
fi

echo "== 退回「直接编译 obfuscation/*.cpp → -shared」"
CXX="$LLVM_DIR/bin/clang++"
[ -x "$CXX" ] || CXX=$(command -v clang++)
rm -rf "$BUILD" && mkdir -p "$BUILD"
CFLAGS=$("$LLVM_DIR/bin/llvm-config" --cxxflags 2>/dev/null || echo "-I$LLVM_DIR/include -std=c++17")
CFLAGS="$CFLAGS -fPIC -O2 -fexceptions -DLLVM_NEXT=1 -DENSIA_TARGET_LLVM_VERSION=21"

mapfile -t SRCS < <(find "$SRC/obfuscation" -name "*.cpp" ! -name "RustCompat.cpp")
echo "源文件数: ${#SRCS[@]}"

# shellcheck disable=SC2086
"$CXX" $CFLAGS -shared -Wl,-soname,libEnsia.so \
  -I"$SRC/obfuscation/include" \
  "${SRCS[@]}" -o "$BUILD/libEnsia.so" 2>&1 | tail -40

ls -lh "$BUILD/libEnsia.so" 2>/dev/null || exit 1
