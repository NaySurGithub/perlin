#include "world/level/chunk/ChunkCodec.hpp"

#include "network/VarInt.hpp"

#include <algorithm>
#include <map>

namespace perlin {
    namespace {

        bool ValidBits(unsigned b) {
            return b == 0 || b == 1 || b == 2 || b == 3 || b == 4 || b == 5 || b == 6 || b == 8 || b == 16;
        }

        size_t WordCount(unsigned bpb) {
            if (bpb == 0)
                return 0;
            unsigned perWord = 32 / bpb;
            return (4096 + perWord - 1) / perWord;
        }

        bool ParseStorage(wire::Reader& r, PaletteStorage& st, const char** err) {
            auto fail = [&](const char* m) {
                if (err)
                    *err = m;
                return false;
            };
            uint8_t hdr = 0;
            if (!r.ReadByte(hdr))
                return fail("storage header truncated");
            if (!(hdr & 1))
                return fail("storage is persistent (NBT) encoded, expected network runtime ids");
            unsigned bpb = hdr >> 1;
            if (!ValidBits(bpb))
                return fail("invalid bits-per-block");
            st.bitsPerBlock = (int)bpb;
            size_t nwords = WordCount(bpb);
            st.words.resize(nwords);
            for (size_t i = 0; i < nwords; i++)
                if (!r.ReadLE32(st.words[i]))
                    return fail("storage words truncated");
            if (bpb == 0) {
                int32_t v = 0;
                if (!r.ReadZigzag32(v))
                    return fail("uniform palette entry truncated");
                st.palette.assign(1, v);
                return true;
            }
            int32_t count = 0;
            if (!r.ReadZigzag32(count) || count <= 0 || count > 4096)
                return fail("bad palette size");
            st.palette.resize((size_t)count);
            for (int32_t i = 0; i < count; i++)
                if (!r.ReadZigzag32(st.palette[(size_t)i]))
                    return fail("palette truncated");
            return true;
        }

    }

    const DimensionInfo& ChunkCodec::Dimension(int id) {
        static const DimensionInfo kOverworld{0, -64, 24};
        static const DimensionInfo kNether{1, 0, 8};
        static const DimensionInfo kEnd{2, 0, 16};
        switch (id) {
        case 1:
            return kNether;
        case 2:
            return kEnd;
        default:
            return kOverworld;
        }
    }

    bool ChunkCodec::ParseSubChunk(const uint8_t* data, size_t size, SubChunkData& out, size_t& consumed,
                                   const char** err) {
        auto fail = [&](const char* m) {
            if (err)
                *err = m;
            return false;
        };
        wire::Reader r(data, size);
        uint8_t version = 0;
        if (!r.ReadByte(version))
            return fail("sub-chunk empty");
        out = SubChunkData{};
        out.version = version;
        if (version == 1) {
            PaletteStorage st;
            if (!ParseStorage(r, st, err))
                return false;
            out.storages.push_back(std::move(st));
        } else if (version == 8 || version == 9) {
            uint8_t n = 0;
            if (!r.ReadByte(n) || n > 8)
                return fail("bad storage count");
            if (version == 9) {
                int8_t y = 0;
                if (!r.ReadInt8(y))
                    return fail("y index truncated");
                out.hasYIndex = true;
                out.yIndex = y;
            }
            for (uint8_t i = 0; i < n; i++) {
                PaletteStorage st;
                if (!ParseStorage(r, st, err))
                    return false;
                out.storages.push_back(std::move(st));
            }
        } else {
            return fail("unknown sub-chunk version");
        }
        consumed = size - r.Remaining();
        return true;
    }

    bool ChunkCodec::ExpandIndices(const PaletteStorage& st, CellIndices& out) {
        if (st.palette.empty())
            return false;
        if (st.bitsPerBlock == 0) {
            out.fill(0);
            return true;
        }
        unsigned bpb = (unsigned)st.bitsPerBlock;
        unsigned perWord = 32 / bpb;
        uint32_t mask = (1u << bpb) - 1;
        if (st.words.size() < WordCount(bpb))
            return false;
        for (unsigned pos = 0; pos < (unsigned)kCells; pos++) {
            uint32_t w = st.words[pos / perWord];
            uint32_t idx = (w >> ((pos % perWord) * bpb)) & mask;
            if (idx >= st.palette.size())
                return false;
            out[pos] = (uint16_t)idx;
        }
        return true;
    }

    bool ChunkCodec::Expand(const PaletteStorage& st, CellIds& out) {
        CellIndices idx;
        if (!ExpandIndices(st, idx))
            return false;
        for (int i = 0; i < kCells; i++)
            out[(size_t)i] = st.palette[idx[(size_t)i]];
        return true;
    }

    void ChunkCodec::WriteStorage(std::vector<uint8_t>& out, const CellIds& cells) {
        std::vector<int32_t> palette;
        std::map<int32_t, uint32_t> lookup;
        std::vector<uint32_t> idx(kCells);
        for (int i = 0; i < kCells; i++) {
            auto it = lookup.find(cells[(size_t)i]);
            if (it == lookup.end()) {
                it = lookup.emplace(cells[(size_t)i], (uint32_t)palette.size()).first;
                palette.push_back(cells[(size_t)i]);
            }
            idx[(size_t)i] = it->second;
        }
        unsigned need = 0;
        while ((1u << need) < palette.size())
            need++;
        unsigned bpb = need == 0 ? 0
                                 : (need <= 1   ? 1
                                    : need <= 2 ? 2
                                    : need <= 3 ? 3
                                    : need <= 4 ? 4
                                    : need <= 5 ? 5
                                    : need <= 6 ? 6
                                    : need <= 8 ? 8
                                                : 16);
        out.push_back((uint8_t)((bpb << 1) | 1));
        if (bpb == 0) {
            wire::WriteZigzag32(out, palette[0]);
            return;
        }
        unsigned perWord = 32 / bpb;
        std::vector<uint32_t> words(WordCount(bpb), 0);
        for (unsigned pos = 0; pos < (unsigned)kCells; pos++)
            words[pos / perWord] |= idx[pos] << ((pos % perWord) * bpb);
        for (uint32_t w : words)
            wire::WriteLE32(out, w);
        wire::WriteZigzag32(out, (int32_t)palette.size());
        for (int32_t v : palette)
            wire::WriteZigzag32(out, v);
    }

    void ChunkCodec::WriteSubChunk(std::vector<uint8_t>& out, int yIndex, const CellIds& cells) {
        out.push_back(9);
        out.push_back(1);
        out.push_back((uint8_t)(int8_t)yIndex);
        WriteStorage(out, cells);
    }

}
