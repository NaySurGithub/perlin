#pragma once
#include "network/packet/LevelChunkPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<LevelChunkPacket> {
        static void Write(WriteStream&, const LevelChunkPacket&) {}
        static bool Read(ReadStream& s, LevelChunkPacket& out);
    };

}
