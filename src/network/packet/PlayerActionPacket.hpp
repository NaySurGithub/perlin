#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct PlayerActionPacket {
        uint64_t runtimeId = 0;
        int32_t action = 0;
        BlockPos position;
        BlockPos resultPosition;
        int32_t face = 0;
    };

    PERLIN_PACKET_ID(PlayerActionPacket, 36);

}
