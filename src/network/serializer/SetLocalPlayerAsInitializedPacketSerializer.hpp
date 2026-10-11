#pragma once
#include "network/packet/SetLocalPlayerAsInitializedPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<SetLocalPlayerAsInitializedPacket> {
        static void Write(WriteStream& s, const SetLocalPlayerAsInitializedPacket& p) {
            s.varu64(p.runtimeId);
        }
        static bool Read(ReadStream& s, SetLocalPlayerAsInitializedPacket& p) {
            s.varu64(p.runtimeId);
            return s.ok;
        }
    };

}
