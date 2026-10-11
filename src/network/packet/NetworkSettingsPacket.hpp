#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct NetworkSettingsPacket {
        uint16_t compressionThreshold = 0;
        uint16_t compressionAlgorithm = 0;
        bool clientThrottle = false;
        uint8_t clientThrottleThreshold = 0;
        float clientThrottleScalar = 0;
    };

    PERLIN_PACKET_ID(NetworkSettingsPacket, 143);

}
