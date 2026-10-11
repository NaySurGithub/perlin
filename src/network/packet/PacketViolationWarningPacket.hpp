#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct PacketViolationWarningPacket {
        int32_t type = 0;
        int32_t severity = 0;
        int32_t packetId = 0;
        std::string context;
    };

    PERLIN_PACKET_ID(PacketViolationWarningPacket, 156);

}
