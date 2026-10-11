# Perlin

Minecraft Bedrock client (SDL3 + bgfx) that joins a Bedrock Dedicated Server over NetherNet.

## Build

Needs CMake 3.28+, a C++23 compiler and OpenSSL. Dependencies are fetched with CPM.

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

On Windows point `OPENSSL_ROOT_DIR` at a vcpkg `x64-windows` install. On Linux install the X11/Wayland/GL
development packages listed in `.github/workflows/build.yml`. On macOS `brew install openssl@3`.

Add `-DPERLIN_BUILD_TESTS=ON` to build and run the unit tests with `ctest`.

## Run

    Perlin --join 127.0.0.1:19132 --name Steve

Without arguments it connects to the `server` set in `options.json`.

Options and key bindings live in `options.json` inside the data folder (`%APPDATA%/Perlin`,
`~/Library/Application Support/Perlin` or `~/.local/share/Perlin`). Bindings use Minecraft-style ids such
as `key.keyboard.w` and `key.mouse.left`.

## Lighting

Block and sky light run 0-15 and are computed per chunk from the 3x3 chunk neighbourhood while meshing.
Emission and opacity come from, in order: the behavior pack in `bedrock-samples/behavior_pack/blocks`
(`minecraft:light_emission`, `minecraft:light_dampening`), `light.json` in the pack folder (seeded from
`resources/light.json`), and finally the block's texture alpha and shape. Shading follows Java: face
shades, smooth AO, smooth light and the 16x16 lightmap driven by `gamma` in `options.json`.

## Shader packs

Terrain and sky use six shaders (`vs_terrain`, `fs_terrain_solid`, `fs_terrain_cutout`,
`fs_terrain_translucent`, `vs_sky`, `fs_sky`) built into the executable. Set `shaderPack` in
`options.json` and put precompiled bgfx binaries at

    <data>/shaderpacks/<pack>/<spirv|glsl|essl|dxbc|metal>/<shader>.bin

to replace any of them. Missing files fall back to the built-in shader. Sources are in `src/shaders`;
every shader sees `u_mvp`, `u_inverseViewProjection`, `u_camera`, `u_fog`, `u_fogColor`, `u_skyColor`,
`u_sunDir`, `u_sunrise`, `u_time`, `s_atlas` and `s_lightmap`.
