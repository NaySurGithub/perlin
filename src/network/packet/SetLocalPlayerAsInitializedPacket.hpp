#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct SetLocalPlayerAsInitializedPacket {
        uint64_t runtimeId = 0;
    };

    PERLIN_PACKET_ID(SetLocalPlayerAsInitializedPacket, 113);

}
