#include "network/serializer/LevelChunkPacketSerializer.hpp"

namespace perlin::net {

    bool PacketSerializer<LevelChunkPacket>::Read(ReadStream& s, LevelChunkPacket& out) {
        auto fail = [&](const char*) {
            s.ok = false;
            return false;
        };
        wire::Reader& r = s.reader();
        out = LevelChunkPacket{};
        if (!r.ReadZigzag32(out.cx) || !r.ReadZigzag32(out.cz))
            return fail("chunk position truncated");
        if (!r.ReadZigzag32(out.dimension))
            return fail("dimension truncated");
        if (!r.ReadVarUint32(out.subChunkCount))
            return fail("sub-chunk count truncated");
        uint8_t hasLimit = 0;
        if (!r.ReadByte(hasLimit))
            return fail("request-limit flag truncated");
        out.hasRequestLimit = hasLimit != 0;
        if (out.hasRequestLimit && !r.ReadZigzag32(out.requestLimit))
            return fail("request limit truncated");
        uint8_t cache = 0;
        if (!r.ReadByte(cache))
            return fail("cache flag truncated");
        out.cacheEnabled = cache != 0;
        uint32_t n = 0;
        if (!r.ReadVarUint32(n) || n > 100000)
            return fail("cache metadata count bad");
        out.blobHashes.resize(n);
        for (uint32_t i = 0; i < n; i++)
            if (!r.ReadLE64(out.blobHashes[i]))
                return fail("cache metadata truncated");
        if (!r.ReadByteArray(out.payload))
            return fail("payload truncated");
        uint8_t biomeUpdate = 0;
        if (r.Remaining() >= 1 && r.ReadByte(biomeUpdate))
            out.isClientBiomeUpdate = biomeUpdate != 0;
        return true;
    }

}
