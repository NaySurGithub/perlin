#pragma once
#include "world/level/Level.hpp"

namespace perlin {
    // Developer aid: PERLIN_PREVIEW=1 renders a generated landscape (hills, water, trees, a lit cabin, a cave)
    // instead of connecting to a server, so shaders can be checked without one. Returns false until the block
    // palette is loaded (call again next frame).
    bool BuildPreviewWorld(Level& level, int radiusChunks);
}
