#pragma once
#include "network/packet/SetTimePacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<SetTimePacket> {
        static void Write(WriteStream& s, const SetTimePacket& p) { s.zz32(p.time); }
        static bool Read(ReadStream& s, SetTimePacket& p) {
            s.zz32(p.time);
            return s.ok;
        }
    };

}
