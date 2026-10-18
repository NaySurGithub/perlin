#!/bin/sh
# Builds a C++ plugin.
#
#   mods/build.sh [--wasm32] [source.cpp] [output dir]
#
# Plugins are 64-bit WebAssembly (memory64) by default, built with Emscripten as a standalone
# WASI module (activate an emsdk install, or point EMSDK at one). --wasm32 builds a classic 32-bit
# plugin with wasi-sdk instead (WASI_SDK_PATH). Perlin runs either kind.
set -e
cd "$(dirname "$0")"
target=wasm64
if [ "$1" = "--wasm32" ]; then
    target=wasm32
    shift
elif [ "$1" = "--wasm64" ]; then
    shift
fi
src="${1:-examples/speedometer.cpp}"
out="${2:-prebuilt}"
name="$(basename "$src" .cpp)"
mkdir -p "$out"
exports="-Wl,--export=perlin_alloc -Wl,--export=perlin_free -Wl,--export=perlin_event -Wl,--export=perlin_subscriptions"

if [ "$target" = wasm32 ]; then
    sdk="${WASI_SDK_PATH:?set WASI_SDK_PATH to a wasi-sdk install (or drop --wasm32 to build with Emscripten)}"
    "$sdk/bin/clang++" --sysroot="$sdk/share/wasi-sysroot" --target=wasm32-wasip1 -std=c++23 -O2 -fno-exceptions -fno-rtti \
        -mexec-model=reactor -Isdk -Wall -Wextra "$src" -o "$out/$name.wasm" $exports -Wl,--strip-all
else
    emxx="em++"
    if ! command -v "$emxx" >/dev/null 2>&1; then
        if [ -n "$EMSDK" ] && [ -x "$EMSDK/upstream/emscripten/em++" ]; then
            emxx="$EMSDK/upstream/emscripten/em++"
        else
            echo "em++ not found: activate emsdk (source emsdk_env.sh) or build with --wasm32 and wasi-sdk" >&2
            exit 1
        fi
    fi
    # STANDALONE_WASM + PURE_WASI: a plain WASI reactor (exports _initialize, imports only WASI and
    # perlin.call), no JavaScript.
    "$emxx" -std=c++23 -O2 -fno-exceptions -fno-rtti -Isdk -Wall -Wextra "$src" -o "$out/$name.wasm" \
        -sMEMORY64=1 -sSTANDALONE_WASM=1 -sPURE_WASI=1 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=1MB -sMAXIMUM_MEMORY=1GB -sSTACK_SIZE=256KB \
        -sERROR_ON_UNDEFINED_SYMBOLS=0 --no-entry \
        -sEXPORTED_FUNCTIONS=_perlin_alloc,_perlin_free,_perlin_event,_perlin_subscriptions
fi
echo "$out/$name.wasm ($target)"
