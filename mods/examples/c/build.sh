#!/bin/sh
set -e
: "${WASI_SDK_PATH:?set WASI_SDK_PATH}"
"$WASI_SDK_PATH/bin/clang" --target=wasm32-wasip1 -O2 -mexec-model=reactor -o "${2:-hello.wasm}" "${1:-hello.c}"
