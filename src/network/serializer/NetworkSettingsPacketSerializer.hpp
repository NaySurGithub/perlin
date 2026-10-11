#pragma once
#include "network/packet/NetworkSettingsPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<NetworkSettingsPacket> {
        static void Write(WriteStream& s, const NetworkSettingsPacket& p) {
            s.le16(p.compressionThreshold);
            s.le16(p.compressionAlgorithm);
            s.boolean(p.clientThrottle);
            s.u8(p.clientThrottleThreshold);
            s.f32(p.clientThrottleScalar);
        }
        static bool Read(ReadStream& s, NetworkSettingsPacket& p) {
            s.le16(p.compressionThreshold);
            s.le16(p.compressionAlgorithm);
            if (s.more()) {
                s.boolean(p.clientThrottle);
                s.u8(p.clientThrottleThreshold);
                s.f32(p.clientThrottleScalar);
            }
            return s.ok;
        }
    };

}
