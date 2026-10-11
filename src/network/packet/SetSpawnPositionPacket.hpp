#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct SetSpawnPositionPacket {
        int32_t spawnType = 0;
        BlockPos position;
        int32_t dimension = 0;
        BlockPos spawnPosition;
    };

    PERLIN_PACKET_ID(SetSpawnPositionPacket, 43);

}
