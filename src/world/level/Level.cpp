#include "world/level/Level.hpp"

#include <algorithm>
#include <cstring>

namespace perlin {
    Chunk::Section& Chunk::Ensure(int section) {
        auto& slot = sections[section];
        if (!slot)
            slot = std::make_unique<Section>();
        return *slot;
    }

    uint16_t Chunk::Get(int x, int y, int z) const {
        if (!InBounds(x, y, z))
            return 0;
        const auto& s = sections[(y - MIN_Y) >> 4];
        return s ? s->blocks[Local(x, y, z)] : 0;
    }

    void Chunk::Set(int x, int y, int z, uint16_t id) {
        if (!InBounds(x, y, z))
            return;
        const int index = (y - MIN_Y) >> 4;
        if (!sections[index] && id == 0)
            return;
        Ensure(index).blocks[Local(x, y, z)] = id;
    }

    uint8_t Chunk::GetBiome(int x, int y, int z) const {
        if (!InBounds(x, y, z))
            return DEFAULT_BIOME;
        const auto& s = sections[(y - MIN_Y) >> 4];
        return s ? s->biomes[Local(x, y, z)] : DEFAULT_BIOME;
    }

    bool Chunk::YRange(int& lo, int& hi) const {
        bool any = false;
        for (int i = 0; i < SECTION_COUNT; i++) {
            const auto& s = sections[i];
            if (!s)
                continue;
            for (int layer = 0; layer < 16; layer++) {
                const uint16_t* row = s->blocks.data() + layer * 256;
                if (std::all_of(row, row + 256, [](uint16_t v) { return v == 0; }))
                    continue;
                const int y = MIN_Y + i * 16 + layer;
                if (!any)
                    lo = y;
                hi = y;
                any = true;
            }
        }
        return any;
    }

    void Chunk::ReadColumn(int x, int z, int yLo, int yHi, uint16_t* blocks, uint8_t* biomes,
                           size_t stride) const {
        for (int y = yLo; y <= yHi; y++) {
            const auto& s = sections[(y - MIN_Y) >> 4];
            if (!s)
                continue;
            const int local = Local(x, y, z);
            const size_t out = static_cast<size_t>(y - yLo) * stride;
            blocks[out] = s->blocks[local];
            biomes[out] = s->biomes[local];
        }
    }

    void Chunk::ReadBlocks(int x, int z, int yLo, int yHi, uint16_t* blocks, size_t stride) const {
        for (int y = yLo; y <= yHi; y++) {
            const auto& s = sections[(y - MIN_Y) >> 4];
            if (s)
                blocks[static_cast<size_t>(y - yLo) * stride] = s->blocks[Local(x, y, z)];
        }
    }

    void Chunk::SetSection(int section, const uint16_t* cells) {
        Section& s = Ensure(section);
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++)
                for (int y = 0; y < 16; y++)
                    s.blocks[(y * 16 + z) * 16 + x] = cells[x * 256 + z * 16 + y];
    }

    void Chunk::SetBiomeSection(int section, const uint8_t* cells) {
        Section& s = Ensure(section);
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++)
                for (int y = 0; y < 16; y++)
                    s.biomes[(y * 16 + z) * 16 + x] = cells[x * 256 + z * 16 + y];
    }

    void Level::Clear() {
        std::lock_guard lock(mutex);
        changeCounter++;
        chunks.clear();
    }

    uint16_t Level::GetBlock(int x, int y, int z) const {
        std::lock_guard lock(mutex);
        const auto it = chunks.find({x >> 4, z >> 4});
        return it == chunks.end() ? 0 : it->second.Get(x & 15, y, z & 15);
    }

    uint8_t Level::GetBiome(int x, int y, int z) const {
        std::lock_guard lock(mutex);
        const auto it = chunks.find({x >> 4, z >> 4});
        return it == chunks.end() ? Chunk::DEFAULT_BIOME : it->second.GetBiome(x & 15, y, z & 15);
    }

    int Level::FindBlockBelow(int x, int z, int fromY, const std::function<bool(uint16_t)>& accept) const {
        std::lock_guard lock(mutex);
        const auto it = chunks.find({x >> 4, z >> 4});
        if (it == chunks.end())
            return Chunk::MIN_Y - 1;
        const Chunk& chunk = it->second;
        const int top = std::min(fromY, Chunk::MIN_Y + Chunk::HEIGHT - 1);
        for (int y = top; y >= Chunk::MIN_Y; y--)
            if (accept(chunk.Get(x & 15, y, z & 15)))
                return y;
        return Chunk::MIN_Y - 1;
    }

    void Level::SetBlock(int x, int y, int z, uint16_t id) {
        std::lock_guard lock(mutex);
        changeCounter++;
        Chunk& chunk = chunks[{x >> 4, z >> 4}];
        chunk.Set(x & 15, y, z & 15, id);
        chunk.version++;
    }

    void Level::EnsureChunk(int cx, int cz) {
        std::lock_guard lock(mutex);
        changeCounter++;
        const auto [it, inserted] = chunks.try_emplace({cx, cz});
        if (inserted)
            it->second.version++;
    }

    void Level::SetSubChunk(int cx, int cz, int absSubY, const uint16_t* cells) {
        const int baseY = absSubY * 16;
        if (baseY < Chunk::MIN_Y || baseY + 16 > Chunk::MIN_Y + Chunk::HEIGHT)
            return;

        std::lock_guard lock(mutex);
        changeCounter++;
        Chunk& chunk = chunks[{cx, cz}];
        chunk.SetSection((baseY - Chunk::MIN_Y) >> 4, cells);
        chunk.version++;

        static constexpr int kNeighbors[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& n : kNeighbors) {
            const auto it = chunks.find({cx + n[0], cz + n[1]});
            if (it != chunks.end())
                it->second.version++;
        }
    }

    void Level::SetBiomeSubChunk(int cx, int cz, int absSubY, const uint8_t* cells) {
        const int baseY = absSubY * 16;
        if (baseY < Chunk::MIN_Y || baseY + 16 > Chunk::MIN_Y + Chunk::HEIGHT)
            return;

        std::lock_guard lock(mutex);
        changeCounter++;
        Chunk& chunk = chunks[{cx, cz}];
        chunk.SetBiomeSection((baseY - Chunk::MIN_Y) >> 4, cells);
        chunk.version++;
    }
}
