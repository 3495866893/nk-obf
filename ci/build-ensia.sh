#!/usr/bin/env bash
# 构建 Ensia 插件（host x86_64），产出两个变体：
#   libEnsia.so          —— LLVM 符号全部 UND，由宿主 clang 提供（首选）
#   libEnsia-linked.so   —— 显式 DT_NEEDED libLLVM.so（兜底）
set -uo pipefail
SRC="${1:-ensia/src}"
BUILD="${2:-ensia-build}"
LLVM_DIR="${3:-$PWD/llvm-build}"

CXX="$LLVM_DIR/bin/clang++"
[ -x "$CXX" ] || CXX=$(command -v clang++)
mkdir -p "$BUILD"

CFLAGS=$("$LLVM_DIR/bin/llvm-config" --cxxflags 2>/dev/null || echo "-I$LLVM_DIR/include -std=c++17")
CFLAGS="$CFLAGS -fPIC -O2 -fexceptions -DLLVM_NEXT=1 -DENSIA_TARGET_LLVM_VERSION=21"

mapfile -t SRCS < <(find "$SRC/obfuscation" -name "*.cpp" ! -name "RustCompat.cpp")
echo "源文件: ${#SRCS[@]}"

echo "== 变体 1：不链 LLVM（符号留 UND）"
# shellcheck disable=SC2086
"$CXX" $CFLAGS -shared -Wl,-soname,libEnsia.so \
  -I"$SRC/obfuscation/include" "${SRCS[@]}" -o "$BUILD/libEnsia.so" 2>&1 | tail -25
ls -lh "$BUILD/libEnsia.so" 2>/dev/null || true

LLVMSO=$(ls "$LLVM_DIR"/lib/libLLVM*.so* 2>/dev/null | head -1 || true)
if [ -n "$LLVMSO" ]; then
  echo "== 变体 2：链接 $LLVMSO"
  LLVM_LINK_DIR=$(dirname "$LLVMSO")
  LLVM_LINK_NAME=$(basename "$LLVMSO" | sed 's/^lib//; s/\.so.*//')
  # shellcheck disable=SC2086
  "$CXX" $CFLAGS -shared -Wl,-soname,libEnsia-linked.so \
    -I"$SRC/obfuscation/include" \
    -L"$LLVM_LINK_DIR" -Wl,--no-as-needed -l"$LLVM_LINK_NAME" \
    -Wl,-rpath,"$LLVM_LINK_DIR" \
    "${SRCS[@]}" -o "$BUILD/libEnsia-linked.so" 2>&1 | tail -25
  ls -lh "$BUILD/libEnsia-linked.so" 2>/dev/null || true
else
  echo "（没有 libLLVM*.so，跳过变体 2）"
fi

echo "导出检查:"
for f in "$BUILD"/libEnsia*.so; do
  printf "  %s  entry=%s  UND_llvm=%s\n" "$(basename "$f")" \
    "$(nm -D "$f" | grep -c llvmGetPassPluginInfo || true)" \
    "$(nm -D "$f" | grep -c ' U .*llvm' || true)"
done
