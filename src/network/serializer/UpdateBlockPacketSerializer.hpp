#pragma once
#include "network/packet/UpdateBlockPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<UpdateBlockPacket> {
        static void Write(WriteStream& s, const UpdateBlockPacket& p) {
            Field(s, p.position);
            s.varu32(p.networkId);
            s.varu32(p.flags);
            s.varu32(p.layer);
        }
        static bool Read(ReadStream& s, UpdateBlockPacket& p) {
            Field(s, p.position);
            s.varu32(p.networkId);
            s.varu32(p.flags);
            s.varu32(p.layer);
            return s.ok;
        }
    };

}
