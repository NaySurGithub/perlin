#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct UpdateBlockPacket {
        BlockPos position;
        uint32_t networkId = 0;
        uint32_t flags = 0;
        uint32_t layer = 0;
    };

    PERLIN_PACKET_ID(UpdateBlockPacket, 21);

}
