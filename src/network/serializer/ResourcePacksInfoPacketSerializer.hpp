#pragma once
#include "network/packet/ResourcePacksInfoPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<ResourcePacksInfoPacket> {
        static void Write(WriteStream&, const ResourcePacksInfoPacket&) {}
        static bool Read(ReadStream&, ResourcePacksInfoPacket&) { return true; }
    };

}
