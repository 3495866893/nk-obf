#!/usr/bin/env bash
# ==============================================================================
# Nk Rust Project Obfuscation Test Runner
# Tests compilation and unit test pass rates of Rust projects using libNk_rust.so
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NK_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PLUGIN_PATH="${NK_PLUGIN:-$NK_ROOT/build/obfuscation/libNk_rust.so}"
PRESET="${1:-csm_vec}"

if [ ! -f "$PLUGIN_PATH" ]; then
    echo "[!] Nk Rust plugin not found at: $PLUGIN_PATH"
    echo "    Building plugin first..."
    cmake --build "$NK_ROOT/build" --target NkRust --parallel "$(nproc)"
fi

echo "================================================================="
echo " Nk Rust Obfuscation Test Suite"
echo " Plugin: $PLUGIN_PATH"
echo " Preset: $PRESET"
echo "================================================================="

PROJECTS=(
    "$NK_ROOT/../bincode"
    "$NK_ROOT/../dtact"
)

TOTAL=0
PASSED=0
FAILED=0

for proj in "${PROJECTS[@]}"; do
    if [ ! -d "$proj" ]; then
        echo "[-] Project directory not found: $proj (Skipping)"
        continue
    fi

    PROJ_NAME="$(basename "$proj")"
    echo ""
    echo "[*] Testing project: $PROJ_NAME ($proj)"
    echo "-----------------------------------------------------------------"
    
    TOTAL=$((TOTAL + 1))
    
    pushd "$proj" > /dev/null
    
    # Run cargo test with Nk pass plugin enabled
    if NK_PRESET="$PRESET" \
       RUSTC_BOOTSTRAP=1 \
       RUSTFLAGS="-Z llvm-plugins=$PLUGIN_PATH -C passes=nk" \
       cargo test --lib --tests; then
        echo "[+] $PROJ_NAME: ALL TESTS PASSED"
        PASSED=$((PASSED + 1))
    else
        echo "[-] $PROJ_NAME: TESTS FAILED"
        FAILED=$((FAILED + 1))
    fi
    
    popd > /dev/null
done

echo ""
echo "================================================================="
echo " Summary: $TOTAL tested, $PASSED passed, $FAILED failed"
echo "================================================================="

if [ "$FAILED" -gt 0 ]; then
    exit 1
fi
