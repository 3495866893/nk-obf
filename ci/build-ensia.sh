#!/usr/bin/env bash
# 构建 Ensia 插件（host x86_64）
# 关键：插件里【不要】静态链 LLVM —— LLVM 符号留给宿主 clang 提供，
#      否则会出现两份 LLVM 状态（TypeContext 不一致）导致加载即崩。
set -uo pipefail
SRC="${1:-ensia/src}"
BUILD="${2:-ensia-build}"
LLVM_DIR="${3:-$PWD/llvm-build}"

CXX="$LLVM_DIR/bin/clang++"
[ -x "$CXX" ] || CXX=$(command -v clang++)

echo "== 方式 1（首选）：直接编译 obfuscation/*.cpp → -shared，不链 LLVM"
rm -rf "$BUILD" && mkdir -p "$BUILD"
CFLAGS=$("$LLVM_DIR/bin/llvm-config" --cxxflags 2>/dev/null || echo "-I$LLVM_DIR/include -std=c++17")
CFLAGS="$CFLAGS -fPIC -O2 -fexceptions -DLLVM_NEXT=1 -DENSIA_TARGET_LLVM_VERSION=21"

mapfile -t SRCS < <(find "$SRC/obfuscation" -name "*.cpp" ! -name "RustCompat.cpp")
echo "源文件数: ${#SRCS[@]}"

# shellcheck disable=SC2086
"$CXX" $CFLAGS -shared -Wl,-soname,libEnsia.so \
  -I"$SRC/obfuscation/include" \
  "${SRCS[@]}" -o "$BUILD/libEnsia.so" 2>&1 | tail -50

if [ -f "$BUILD/libEnsia.so" ]; then
  ls -lh "$BUILD/libEnsia.so"
  echo "导出符号检查:"
  nm -D "$BUILD/libEnsia.so" | grep -c llvmGetPassPluginInfo
  echo "LLVM 符号应为 UND（未定义，由宿主提供）:"
  nm -D "$BUILD/libEnsia.so" | grep -c " U .*llvm" || true
  exit 0
fi

echo "== 方式 2（回退）：cmake 只编 Ensia 目标（会静态链 LLVM，体积大）"
cmake -S "$SRC" -B "$BUILD.cmake" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR="$LLVM_DIR/lib/cmake/llvm" 2>&1 | tail -20
ninja -C "$BUILD.cmake" Ensia 2>&1 | tail -60
find "$BUILD.cmake" -name "*.so" -exec cp {} "$BUILD/" \;
find "$BUILD" -name "*.so" -exec ls -lh {} \;
