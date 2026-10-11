#pragma once
#include "network/packet/DisconnectPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<DisconnectPacket> {
        static void Write(WriteStream& s, const DisconnectPacket& p) {
            s.zz32(p.reason);
            s.boolean(p.hideMessage);
            if (!p.hideMessage) {
                s.str(p.message);
                s.str(p.filteredMessage);
            }
        }
        static bool Read(ReadStream& s, DisconnectPacket& p) {
            s.zz32(p.reason);
            s.boolean(p.hideMessage);
            if (!p.hideMessage && s.more()) {
                s.str(p.message);
                if (s.more())
                    s.str(p.filteredMessage);
            }
            return s.ok;
        }
    };

}
