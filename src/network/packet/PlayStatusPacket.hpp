#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct PlayStatusPacket {
        int32_t status = 0;
    };

    PERLIN_PACKET_ID(PlayStatusPacket, 2);

}
