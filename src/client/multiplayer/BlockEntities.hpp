#pragma once
#include "client/ui/Json.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace perlin {
    struct SignFace {
        std::string text;
        uint32_t color = 0xFF000000u;
        bool glow = false;
    };

    struct BlockEntity {
        std::string id;
        int color = -1;
        SignFace front, back;
        bool waxed = false;
    };

    class BlockEntityStore {
    public:
        void Set(int x, int y, int z, const ui::Json& nbt);
        bool Get(int x, int y, int z, BlockEntity& out) const;
        void Remove(int x, int y, int z);
        void RemoveChunk(int cx, int cz);
        void Clear();
        size_t Size() const;

    private:
        static uint64_t Key(int x, int y, int z) {
            return (static_cast<uint64_t>(static_cast<uint32_t>(x) & 0x3FFFFFFu) << 38) |
                   (static_cast<uint64_t>(static_cast<uint32_t>(z) & 0x3FFFFFFu) << 12) | (static_cast<uint64_t>(y + 2048) & 0xFFFu);
        }
        mutable std::mutex mutex;
        std::unordered_map<uint64_t, BlockEntity> entities;
    };
}
