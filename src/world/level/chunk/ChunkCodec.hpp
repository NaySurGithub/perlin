#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace perlin {

    struct DimensionInfo {
        int id = 0;
        int minY = -64;
        int slots = 24;
        int MinSubChunk() const { return minY >> 4; }
    };

    struct PaletteStorage {
        int bitsPerBlock = 0;
        std::vector<uint32_t> words;
        std::vector<int32_t> palette;
    };

    struct SubChunkData {
        int version = 0;
        bool hasYIndex = false;
        int yIndex = 0;
        std::vector<PaletteStorage> storages;
    };

    class ChunkCodec {
    public:
        static constexpr int kCells = 4096;
        using CellIds = std::array<int32_t, kCells>;

        static const DimensionInfo& Dimension(int dimensionId);

        static bool ParseSubChunk(const uint8_t* data, size_t size, SubChunkData& out, size_t& consumed,
                                  const char** err = nullptr);

        using CellIndices = std::array<uint16_t, kCells>;

        static bool ExpandIndices(const PaletteStorage& st, CellIndices& out);
        static bool Expand(const PaletteStorage& st, CellIds& out);

        static void WriteStorage(std::vector<uint8_t>& out, const CellIds& cells);
        static void WriteSubChunk(std::vector<uint8_t>& out, int yIndex, const CellIds& cells);
    };

}
