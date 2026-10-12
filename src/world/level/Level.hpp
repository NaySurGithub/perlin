#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace perlin {
    struct ChunkPos {
        int x, z;
        bool operator==(const ChunkPos& o) const { return x == o.x && z == o.z; }
    };

    struct ChunkPosHash {
        size_t operator()(const ChunkPos& p) const {
            return std::hash<int64_t>()((static_cast<int64_t>(p.x) << 32) | static_cast<uint32_t>(p.z));
        }
    };

    class Chunk {
    public:
        static constexpr int SIZE = 16;
        static constexpr int MIN_Y = -64;
        static constexpr int HEIGHT = 384;
        static constexpr int SECTION_COUNT = HEIGHT / 16;
        static constexpr uint8_t DEFAULT_BIOME = 1;

        uint16_t Get(int x, int y, int z) const;
        void Set(int x, int y, int z, uint16_t id);
        uint8_t GetBiome(int x, int y, int z) const;

        bool YRange(int& lo, int& hi) const;
        void ReadColumn(int x, int z, int yLo, int yHi, uint16_t* blocks, uint8_t* biomes,
                        size_t stride) const;

        void ReadBlocks(int x, int z, int yLo, int yHi, uint16_t* blocks, size_t stride) const;

        void SetSection(int section, const uint16_t* cells);
        void SetBiomeSection(int section, const uint8_t* cells);

        uint64_t version = 0;

    private:
        struct Section {
            std::array<uint16_t, 4096> blocks{};
            std::array<uint8_t, 4096> biomes;
            Section() { biomes.fill(DEFAULT_BIOME); }
        };

        static int Local(int x, int y, int z) { return (((y & 15) << 4) + z) * 16 + x; }
        static bool InBounds(int x, int y, int z) {
            return x >= 0 && x < SIZE && z >= 0 && z < SIZE && y >= MIN_Y && y < MIN_Y + HEIGHT;
        }
        Section& Ensure(int section);

        std::array<std::unique_ptr<Section>, SECTION_COUNT> sections;
    };

    class Level {
    public:
        uint16_t GetBlock(int x, int y, int z) const;
        // Like GetBlock, but reports whether the chunk exists (unloaded chunks read as air).
        uint16_t GetBlock(int x, int y, int z, bool& loaded) const;
        uint8_t GetBiome(int x, int y, int z) const;
        void SetBlock(int x, int y, int z, uint16_t id);
        void EnsureChunk(int cx, int cz);
        bool RemoveChunk(int cx, int cz);
        void SetSubChunk(int cx, int cz, int absSubY, const uint16_t* cells);
        void SetBiomeSubChunk(int cx, int cz, int absSubY, const uint8_t* cells);
        void Clear();

        int FindBlockBelow(int x, int z, int fromY, const std::function<bool(uint16_t)>& accept) const;

        std::unordered_map<ChunkPos, Chunk, ChunkPosHash> chunks;
        mutable std::mutex mutex;
        std::atomic<uint64_t> changeCounter{0};
    };
}
