#include "network/serializer/SubChunkPacketSerializer.hpp"

namespace perlin::net {

    namespace {

        struct EntryLayout {
            bool payloadFlag;
            int heightMap;
            bool blobFlag;
        };

        const EntryLayout kLayouts[] = {
            {true, 1, true},  {true, 1, false},  {true, 2, true},  {true, 2, false},
            {true, 0, true},  {true, 0, false},  {false, 1, true}, {false, 1, false},
            {false, 2, true}, {false, 2, false}, {false, 0, true}, {false, 0, false},
        };
        constexpr int kLayoutCount = sizeof(kLayouts) / sizeof(kLayouts[0]);
        int g_lastLayout = 0;

        bool ReadPresence(wire::Reader& r, bool& present) {
            uint8_t b = 0;
            if (!r.ReadByte(b) || b > 1)
                return false;
            present = b == 1;
            return true;
        }

        bool SkipNested(wire::Reader& r) {
            uint32_t outer = 0;
            if (!r.ReadVarUint32(outer) || outer > 64)
                return false;
            for (uint32_t i = 0; i < outer; i++) {
                uint32_t inner = 0;
                if (!r.ReadVarUint32(inner) || inner > 64 || r.Remaining() < inner)
                    return false;
                r.Skip(inner);
            }
            return true;
        }

        bool ReadHeightMap(wire::Reader& r, int mode, uint8_t& type, uint8_t maxType) {
            if (!r.ReadByte(type) || type > maxType)
                return false;
            if (mode == 0) {
                if (type == 1) {
                    if (r.Remaining() < 256)
                        return false;
                    r.Skip(256);
                }
                return true;
            }
            bool present = false;
            if (!ReadPresence(r, present))
                return false;
            if (!present)
                return true;
            if (mode == 1) {
                if (r.Remaining() < 256)
                    return false;
                r.Skip(256);
                return true;
            }
            return SkipNested(r);
        }

        bool ReadEntry(wire::Reader& r, bool cache, const EntryLayout& L, SubChunkEntry& e) {
            if (!r.ReadInt8(e.dx) || !r.ReadInt8(e.dy) || !r.ReadInt8(e.dz) || !r.ReadByte(e.result))
                return false;
            if (e.result > 6)
                return false;
            bool hasPayload = true;
            if (L.payloadFlag && !ReadPresence(r, hasPayload))
                return false;
            if (hasPayload && !r.ReadByteArray(e.payload))
                return false;
            if (!e.payload.empty() && e.payload[0] != 1 && e.payload[0] != 8 && e.payload[0] != 9)
                return false;
            if (!ReadHeightMap(r, L.heightMap, e.heightMapType, 3) ||
                !ReadHeightMap(r, L.heightMap, e.renderHeightMapType, 4))
                return false;
            if (L.blobFlag) {
                bool present = false;
                if (!ReadPresence(r, present))
                    return false;
                uint64_t id = 0;
                if (present && !r.ReadLE64(id))
                    return false;
            } else if (cache) {
                uint64_t id = 0;
                if (!r.ReadLE64(id))
                    return false;
            }
            return true;
        }

    }

    bool PacketSerializer<SubChunkPacket>::Read(ReadStream& s, SubChunkPacket& out) {
        auto fail = [&](const char*) {
            s.ok = false;
            return false;
        };
        wire::Reader& r = s.reader();
        out = SubChunkPacket{};
        uint8_t cache = 0;
        if (!r.ReadByte(cache))
            return fail("cache flag truncated");
        out.cacheEnabled = cache != 0;
        uint32_t cx = 0, cy = 0, cz = 0;
        if (!r.ReadZigzag32(out.dimension) || !r.ReadLE32(cx) || !r.ReadLE32(cy) || !r.ReadLE32(cz))
            return fail("header truncated");
        out.baseX = (int32_t)cx;
        out.baseY = (int32_t)cy;
        out.baseZ = (int32_t)cz;
        uint32_t count = 0;
        if (!r.ReadVarUint32(count) || count > 8192)
            return fail("entry count bad");

        const uint8_t* entriesStart = r.Cursor();
        size_t entriesSize = r.Remaining();

        for (int attempt = 0; attempt < kLayoutCount; attempt++) {
            int li = attempt == 0 ? g_lastLayout : (attempt - 1 < g_lastLayout ? attempt - 1 : attempt);
            if (li >= kLayoutCount)
                continue;
            wire::Reader rr(entriesStart, entriesSize);
            std::vector<SubChunkEntry> entries;
            entries.reserve(count);
            bool ok = true;
            for (uint32_t i = 0; i < count && ok; i++) {
                SubChunkEntry e;
                ok = ReadEntry(rr, out.cacheEnabled, kLayouts[li], e);
                if (ok)
                    entries.push_back(std::move(e));
            }
            if (ok && rr.Remaining() == 0) {
                out.entries = std::move(entries);
                out.layout = li;
                out.exact = true;
                g_lastLayout = li;
                r.Skip(entriesSize);
                return true;
            }
        }

        {
            wire::Reader rr(entriesStart, entriesSize);
            SubChunkEntry e;
            uint8_t present = 0;
            if (count > 0 && rr.ReadInt8(e.dx) && rr.ReadInt8(e.dy) && rr.ReadInt8(e.dz) &&
                rr.ReadByte(e.result) && e.result <= 6 && rr.ReadByte(present) && present <= 1) {
                bool ok = true;
                if (present)
                    ok = rr.ReadByteArray(e.payload) &&
                         (e.payload.empty() || e.payload[0] == 1 || e.payload[0] == 8 || e.payload[0] == 9);
                if (ok) {
                    out.entries.push_back(std::move(e));
                    out.layout = 99;
                    out.exact = false;
                    r.Skip(entriesSize);
                    return true;
                }
            }
        }
        return fail("no known SubChunk entry layout matches");
    }

}
