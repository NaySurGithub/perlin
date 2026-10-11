#pragma once
#include "network/packet/SubChunkPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<SubChunkPacket> {
        static void Write(WriteStream&, const SubChunkPacket&) {}
        static bool Read(ReadStream& s, SubChunkPacket& out);
    };

}
