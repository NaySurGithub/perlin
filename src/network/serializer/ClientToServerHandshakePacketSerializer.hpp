#pragma once
#include "network/packet/ClientToServerHandshakePacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<ClientToServerHandshakePacket> {
        static void Write(WriteStream&, const ClientToServerHandshakePacket&) {}
        static bool Read(ReadStream&, ClientToServerHandshakePacket&) { return true; }
    };

}
