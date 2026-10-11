#pragma once
#include "network/packet/ServerToClientHandshakePacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<ServerToClientHandshakePacket> {
        static void Write(WriteStream& s, const ServerToClientHandshakePacket& p) { s.str(p.token); }
        static bool Read(ReadStream& s, ServerToClientHandshakePacket& p) {
            if (s.more())
                s.str(p.token);
            return s.ok;
        }
    };

}
