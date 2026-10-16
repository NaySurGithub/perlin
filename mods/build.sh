#!/bin/sh
set -e
cd "$(dirname "$0")"
sdk="${WASI_SDK_PATH:?set WASI_SDK_PATH to a wasi-sdk install}"
src="${1:-examples/speedometer.cpp}"
out="${2:-prebuilt}"
name="$(basename "$src" .cpp)"
mkdir -p "$out"
"$sdk/bin/clang++" --sysroot="$sdk/share/wasi-sysroot" --target=wasm32-wasip1 -std=c++23 -O2 -fno-exceptions -fno-rtti \
    -mexec-model=reactor -Isdk -Wall -Wextra "$src" -o "$out/$name.wasm" \
    -Wl,--export=perlin_alloc -Wl,--export=perlin_free -Wl,--export=perlin_event -Wl,--export=perlin_subscriptions -Wl,--strip-all
echo "$out/$name.wasm"
