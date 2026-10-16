#!/bin/sh
set -e
cd "$(dirname "$0")"
out="${2:-build}"
mkdir -p "$out"
src="${1:-examples/speedometer.c}"
name="$(basename "$src" .c)"
clang --target=wasm32 -O2 -nostdlib -fno-builtin -ffreestanding -Wl,--no-entry -Wl,--export-dynamic -Wl,--allow-undefined -Wl,-z,stack-size=16384 -Wl,--initial-memory=131072 -fuse-ld=lld "$src" -o "$out/$name.wasm"
echo "$out/$name.wasm"
