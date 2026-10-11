#pragma once
#include "network/packet/StartGamePacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<StartGamePacket> {
        static void Write(WriteStream& s, const StartGamePacket& p) {
            s.zz64(p.entityId);
            s.varu64(p.runtimeId);
            s.zz32(p.gameMode);
            Field(s, p.position);
            s.f32(p.pitch);
            s.f32(p.yaw);
            s.rest(p.tail.bytes);
        }
        static bool Read(ReadStream& s, StartGamePacket& p) {
            s.zz64(p.entityId);
            s.varu64(p.runtimeId);
            s.zz32(p.gameMode);
            Field(s, p.position);
            s.f32(p.pitch);
            s.f32(p.yaw);
            s.rest(p.tail.bytes);
            return s.ok;
        }
    };

}
