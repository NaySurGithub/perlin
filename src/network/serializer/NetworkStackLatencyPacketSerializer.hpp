#pragma once
#include "network/packet/NetworkStackLatencyPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<NetworkStackLatencyPacket> {
        static void Write(WriteStream& s, const NetworkStackLatencyPacket& p) {
            s.le64(p.timestamp);
            s.boolean(p.needResponse);
        }
        static bool Read(ReadStream& s, NetworkStackLatencyPacket& p) {
            s.le64(p.timestamp);
            s.boolean(p.needResponse);
            return s.ok;
        }
    };

}
