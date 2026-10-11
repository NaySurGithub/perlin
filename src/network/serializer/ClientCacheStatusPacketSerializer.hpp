#pragma once
#include "network/packet/ClientCacheStatusPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<ClientCacheStatusPacket> {
        static void Write(WriteStream& s, const ClientCacheStatusPacket& p) { s.boolean(p.enabled); }
        static bool Read(ReadStream& s, ClientCacheStatusPacket& p) {
            s.boolean(p.enabled);
            return s.ok;
        }
    };

}
