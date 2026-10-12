#pragma once
#include "client/renderer/Tessellator.hpp"
#include "world/level/Level.hpp"

#include <array>
#include <functional>
#include <unordered_map>

namespace perlin {
    class BlockLibrary;
    class BiomeTints;

    // Far-terrain level of detail ("Voxy"-style): every chunk the client has fully received is reduced to a 4x4
    // grid of surface samples (height, colour, water). The store outlives the chunk itself, so terrain that left
    // the loaded area keeps drawing as cheap tile meshes out to the full render distance.
    struct LodCell {
        int16_t y = 0;     // top surface (block y + 1); water surface when `water`
        uint8_t r = 0, g = 0, b = 0;
        uint8_t water = 0;
        uint8_t depth = 0; // blocks of water above the floor, capped
    };

    struct LodColumn {
        static constexpr int kCells = 4;       // cells per chunk side
        static constexpr int kCellSize = 16 / kCells;
        std::array<LodCell, kCells * kCells> cells;
    };

    class LodStore {
    public:
        // Chunks within this distance of the player are always real geometry; LOD only exists beyond it, and only
        // when the render distance is larger than this.
        static constexpr int kRealChunks = 32;
        static constexpr int kTileChunks = 4;  // a tile mesh covers 4x4 chunks

        static int TileOf(int chunk) { return chunk >= 0 ? chunk / kTileChunks : -((-chunk + kTileChunks - 1) / kTileChunks); }

        // Samples a chunk of `level`; returns true when the stored data changed.
        bool Capture(const Level& level, const ChunkPos& pos, const BlockLibrary& blocks, const BiomeTints& tints);
        void Remove(const ChunkPos& pos) { columns.erase(pos); }
        void Clear() { columns.clear(); colours.clear(); }
        // Drops the colour cache (textures changed); stored cells keep their colours until re-captured.
        void InvalidateColours() { colours.clear(); }
        const LodColumn* Find(const ChunkPos& pos) const {
            const auto it = columns.find(pos);
            return it == columns.end() ? nullptr : &it->second;
        }
        size_t Size() const { return columns.size(); }
        // Removes columns farther than `radius` chunks (square) from the centre.
        void Evict(int centreX, int centreZ, int radius, std::vector<ChunkPos>& removed);
        const std::unordered_map<ChunkPos, LodColumn, ChunkPosHash>& All() const { return columns; }

        // Builds the mesh of one tile. `hidden(pos)` is true for chunks currently drawn at full detail; `stride` is
        // the cell stride (1 = 4 blocks, 2 = 8 blocks). Returns false when the tile has nothing to draw.
        bool BuildTile(int tileX, int tileZ, int stride, const std::function<bool(const ChunkPos&)>& hidden,
                       Tessellator& out) const;

    private:
        struct Rgb { uint8_t r, g, b; };
        Rgb ColourOf(uint16_t id, int biome, bool top, const BlockLibrary& blocks, const BiomeTints& tints);
        std::unordered_map<ChunkPos, LodColumn, ChunkPosHash> columns;
        std::unordered_map<uint32_t, Rgb> colours;
    };
}
