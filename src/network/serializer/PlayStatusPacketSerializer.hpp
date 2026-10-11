#pragma once
#include "network/packet/PlayStatusPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<PlayStatusPacket> {
        static void Write(WriteStream& s, const PlayStatusPacket& p) { s.be32(p.status); }
        static bool Read(ReadStream& s, PlayStatusPacket& p) {
            s.be32(p.status);
            return s.ok;
        }
    };

}
