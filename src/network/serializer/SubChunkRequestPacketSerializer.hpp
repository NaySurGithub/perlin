#pragma once
#include "network/packet/SubChunkRequestPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<SubChunkRequestPacket> {
        static void Write(WriteStream& s, const SubChunkRequestPacket& p) {
            s.zz32(p.dimension);
            s.varu32(static_cast<uint32_t>(p.offsets.size()));
            for (const auto& offset : p.offsets) {
                s.i8(offset[0]);
                s.i8(offset[1]);
                s.i8(offset[2]);
            }
            s.le32(static_cast<uint32_t>(p.baseX));
            s.le32(static_cast<uint32_t>(p.baseY));
            s.le32(static_cast<uint32_t>(p.baseZ));
        }
        static bool Read(ReadStream& s, SubChunkRequestPacket& p) {
            s.zz32(p.dimension);
            uint32_t count = 0;
            if (!s.count(count))
                return false;
            p.offsets.assign(count, {});
            for (auto& offset : p.offsets) {
                s.i8(offset[0]);
                s.i8(offset[1]);
                s.i8(offset[2]);
            }
            uint32_t x = 0, y = 0, z = 0;
            s.le32(x);
            s.le32(y);
            s.le32(z);
            p.baseX = static_cast<int32_t>(x);
            p.baseY = static_cast<int32_t>(y);
            p.baseZ = static_cast<int32_t>(z);
            return s.ok;
        }
    };

}
