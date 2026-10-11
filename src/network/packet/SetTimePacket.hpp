#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct SetTimePacket {
        int32_t time = 0;
    };

    PERLIN_PACKET_ID(SetTimePacket, 10);

}
