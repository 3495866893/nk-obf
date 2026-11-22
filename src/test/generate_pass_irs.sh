#!/usr/bin/env bash
set -euo pipefail

NK_ROOT="/home/user/dev/nk"
PLUGIN_LIB="${NK_ROOT}/build/obfuscation/libNk.so"
OUT_DIR="${NK_ROOT}/test/inspect_passes"
SRC="${NK_ROOT}/test/min_obf_example.c"

mkdir -p "$OUT_DIR"

echo "[*] Generating Baseline (unobfuscated -O1)..."
clang -O1 -S -emit-llvm "$SRC" -o "$OUT_DIR/baseline.ll"
clang -O1 -S "$SRC" -o "$OUT_DIR/baseline.s"

PASSES=(
  "SUBOBF=1"
  "MBAOBF=1"
  "STRCRY=1"
  "SPLITOBF=1"
  "BCFOBF=1"
  "CFFOBF=1"
  "CONSTENC=1"
  "INDIBRAN=1"
  "FUNCWRA=1"
  "FCO=1"
  "VOBF=1"
  "ADB=1"
)

for p in "${PASSES[@]}"; do
  pass_name="${p%%=*}"
  echo "[*] Generating $pass_name..."
  env NK=1 "$p" clang -fpass-plugin="${PLUGIN_LIB}" -O1 -S -emit-llvm "$SRC" -o "$OUT_DIR/${pass_name}.ll"
  env NK=1 "$p" clang -fpass-plugin="${PLUGIN_LIB}" -O1 -S "$SRC" -o "$OUT_DIR/${pass_name}.s"
done

echo "[*] Generating Combined all passes..."
env NK=1 SUBOBF=1 MBAOBF=1 STRCRY=1 SPLITOBF=1 BCFOBF=1 CFFOBF=1 CONSTENC=1 INDIBRAN=1 FUNCWRA=1 FCO=1 \
  clang -fpass-plugin="${PLUGIN_LIB}" -O1 -S -emit-llvm "$SRC" -o "$OUT_DIR/combined.ll"
env NK=1 SUBOBF=1 MBAOBF=1 STRCRY=1 SPLITOBF=1 BCFOBF=1 CFFOBF=1 CONSTENC=1 INDIBRAN=1 FUNCWRA=1 FCO=1 \
  clang -fpass-plugin="${PLUGIN_LIB}" -O1 -S "$SRC" -o "$OUT_DIR/combined.s"

echo "[+] Done generating IR and ASM for all passes."
