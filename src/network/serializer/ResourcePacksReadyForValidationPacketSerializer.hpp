#pragma once
#include "network/packet/ResourcePacksReadyForValidationPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<ResourcePacksReadyForValidationPacket> {
        static void Write(WriteStream&, const ResourcePacksReadyForValidationPacket&) {}
        static bool Read(ReadStream&, ResourcePacksReadyForValidationPacket&) { return true; }
    };

}
