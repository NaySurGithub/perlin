#pragma once
#include "network/packet/RequestNetworkSettingsPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<RequestNetworkSettingsPacket> {
        static void Write(WriteStream& s, const RequestNetworkSettingsPacket& p) { s.be32(p.protocol); }
        static bool Read(ReadStream& s, RequestNetworkSettingsPacket& p) {
            s.be32(p.protocol);
            return s.ok;
        }
    };

}
