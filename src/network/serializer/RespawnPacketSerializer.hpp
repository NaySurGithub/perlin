#pragma once
#include "network/packet/RespawnPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<RespawnPacket> {
        static void Write(WriteStream& s, const RespawnPacket& p) {
            Field(s, p.position);
            s.u8(p.state);
            s.varu64(p.runtimeId);
        }
        static bool Read(ReadStream& s, RespawnPacket& p) {
            Field(s, p.position);
            s.u8(p.state);
            if (s.more())
                s.varu64(p.runtimeId);
            return s.ok;
        }
    };

}
