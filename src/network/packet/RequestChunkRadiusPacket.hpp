#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct RequestChunkRadiusPacket {
        int32_t radius = 0;
        uint8_t maxRadius = 8;
    };

    PERLIN_PACKET_ID(RequestChunkRadiusPacket, 69);

}
