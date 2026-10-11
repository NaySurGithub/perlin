#pragma once
#include "network/packet/PlayerActionPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<PlayerActionPacket> {
        static void Write(WriteStream& s, const PlayerActionPacket& p) {
            s.varu64(p.runtimeId);
            s.zz32(p.action);
            Field(s, p.position);
            Field(s, p.resultPosition);
            s.zz32(p.face);
        }
        static bool Read(ReadStream& s, PlayerActionPacket& p) {
            s.varu64(p.runtimeId);
            s.zz32(p.action);
            Field(s, p.position);
            Field(s, p.resultPosition);
            s.zz32(p.face);
            return s.ok;
        }
    };

}
