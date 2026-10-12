#pragma once
#include "world/level/Level.hpp"
#include "world/level/lighting/LightRegistry.hpp"

namespace perlin {
    class Tessellator;
    class BlockLibrary;
    class BiomeTints;

    int BlockLightOpacity(BlockLibrary& library, uint16_t id);
    // Face shapes for blocks Java lights by shape (stairs, slabs, farmland, paths, snow layers).
    bool BlockFaceOcclusion(uint16_t id, FaceOcclusion& out);

    void MeshProfilePrint();
    // Call when anything the mesher memoises per block id (textures, models, tints) changed.
    void MesherInvalidateCaches();
    void BuildChunkMesh(Level& level, const ChunkPos& pos, Tessellator& tess, BlockLibrary* library,
                        BiomeTints* tints);
}
