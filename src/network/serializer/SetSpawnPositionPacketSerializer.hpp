#pragma once
#include "network/packet/SetSpawnPositionPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<SetSpawnPositionPacket> {
        static void Write(WriteStream& s, const SetSpawnPositionPacket& p) {
            s.zz32(p.spawnType);
            Field(s, p.position);
            s.zz32(p.dimension);
            Field(s, p.spawnPosition);
        }
        static bool Read(ReadStream& s, SetSpawnPositionPacket& p) {
            s.zz32(p.spawnType);
            Field(s, p.position);
            s.zz32(p.dimension);
            Field(s, p.spawnPosition);
            return s.ok;
        }
    };

}
