#pragma once
#include "network/packet/ResourcePackStackPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<ResourcePackStackPacket> {
        static void Write(WriteStream&, const ResourcePackStackPacket&) {}
        static bool Read(ReadStream&, ResourcePackStackPacket&) { return true; }
    };

}
