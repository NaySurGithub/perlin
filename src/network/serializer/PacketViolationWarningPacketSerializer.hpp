#pragma once
#include "network/packet/PacketViolationWarningPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<PacketViolationWarningPacket> {
        static void Write(WriteStream& s, const PacketViolationWarningPacket& p) {
            s.zz32(p.type);
            s.zz32(p.severity);
            s.zz32(p.packetId);
            s.str(p.context);
        }
        static bool Read(ReadStream& s, PacketViolationWarningPacket& p) {
            s.zz32(p.type);
            s.zz32(p.severity);
            s.zz32(p.packetId);
            s.str(p.context);
            return s.ok;
        }
    };

}
