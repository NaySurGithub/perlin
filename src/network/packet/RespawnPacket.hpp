#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct RespawnPacket {
        Vec3 position;
        uint8_t state = 0;
        uint64_t runtimeId = 0;
    };

    PERLIN_PACKET_ID(RespawnPacket, 45);

}
