#pragma once
#include "world/level/Level.hpp"

namespace perlin {
    class Tessellator;
    class BlockLibrary;
    class BiomeTints;

    int BlockLightOpacity(BlockLibrary& library, uint16_t id);

    void BuildChunkMesh(Level& level, const ChunkPos& pos, Tessellator& tess, BlockLibrary* library,
                        BiomeTints* tints);
}
