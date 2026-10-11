#pragma once
#include "network/packet/RequestChunkRadiusPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<RequestChunkRadiusPacket> {
        static void Write(WriteStream& s, const RequestChunkRadiusPacket& p) {
            s.zz32(p.radius);
            s.u8(p.maxRadius);
        }
        static bool Read(ReadStream& s, RequestChunkRadiusPacket& p) {
            s.zz32(p.radius);
            s.u8(p.maxRadius);
            return s.ok;
        }
    };

}
