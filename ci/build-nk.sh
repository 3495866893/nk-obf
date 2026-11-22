#!/usr/bin/env bash
# 构建 Nk 插件（host x86_64）。用系统 clang++/g++ 编译，只借用自建 LLVM 的头文件。
set -euo pipefail
SRC="${1:-nk/src}"
BUILD="${2:-nk-build}"
LLVM_DIR="${3:-$PWD/llvm-build}"
LLVM_SRC="${4:-$PWD/llvm-src}"

# 自建 clang 的默认 target triple 是 "unknown"，编宿主插件会报
# "unknown target triple 'unknown'"，所以插件统一用系统编译器
if [ -n "${PLUGIN_CXX:-}" ]; then
  CXX="$PLUGIN_CXX"
elif command -v clang++ >/dev/null 2>&1; then
  CXX=clang++
elif command -v g++ >/dev/null 2>&1; then
  CXX=g++
else
  echo "!! 没有可用的宿主 C++ 编译器"; exit 1
fi
echo "插件编译器: $CXX ($($CXX --version | head -1))"

mkdir -p "$BUILD"
mapfile -t SRCS < <(find "$SRC/obfuscation" -name "*.cpp" ! -name "RustCompat.cpp")
echo "源文件: ${#SRCS[@]} 个"
[ ${#SRCS[@]} -gt 0 ] || { echo "!! 没找到 nk 源码"; exit 1; }

INC=(-I"$LLVM_SRC/llvm/include" -I"$LLVM_DIR/include" -I"$SRC/obfuscation" -I"$SRC/obfuscation/include" -I"$SRC")
HASH=$(git -C "$PWD" rev-parse HEAD 2>/dev/null || echo unknown)
FLAGS=(-fPIC -O2 -std=c++17 -fno-rtti -fexceptions -DLLVM_NEXT=1
       -DNK_TARGET_LLVM_VERSION=21 -w
       "-DGIT_COMMIT_HASH=\"$HASH\"" )

echo "== 编译插件（LLVM 符号留 UND）"
"$CXX" "${FLAGS[@]}" -shared -Wl,-soname,libNk.so "${INC[@]}" \
  "${SRCS[@]}" -o "$BUILD/libNk.so"

[ -f "$BUILD/libNk.so" ] || { echo "!! libNk.so 没生成"; exit 1; }
ls -lh "$BUILD/libNk.so"

echo "== 校验导出入口"
nm -D "$BUILD/libNk.so" | grep -q llvmGetPassPluginInfo \
  && echo "OK: 导出 llvmGetPassPluginInfo" \
  || { echo "!! 缺少 llvmGetPassPluginInfo"; exit 1; }

UND=$(nm -D "$BUILD/libNk.so" | grep -c " U .*llvm" || true)
echo "UND llvm 符号: $UND"
