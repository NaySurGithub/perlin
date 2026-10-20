#!/bin/sh
# Builds the C example. 64-bit WebAssembly by default (Emscripten's emcc); --wasm32 uses wasi-sdk.
set -e
cd "$(dirname "$0")"
if [ "$1" = "--wasm32" ]; then
    shift
    : "${WASI_SDK_PATH:?set WASI_SDK_PATH}"
    "$WASI_SDK_PATH/bin/clang" --target=wasm32-wasip1 -O2 -mexec-model=reactor -o "${2:-hello.wasm}" "${1:-hello.c}"
else
    emcc -O2 -sMEMORY64=1 -sSTANDALONE_WASM=1 -sPURE_WASI=1 -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=1GB -sERROR_ON_UNDEFINED_SYMBOLS=0 --no-entry \
        -sEXPORTED_FUNCTIONS=_perlin_alloc,_perlin_free,_perlin_event,_perlin_subscriptions -o "${2:-hello.wasm}" "${1:-hello.c}"
fi
