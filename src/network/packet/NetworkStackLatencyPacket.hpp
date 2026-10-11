#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct NetworkStackLatencyPacket {
        uint64_t timestamp = 0;
        bool needResponse = false;
    };

    PERLIN_PACKET_ID(NetworkStackLatencyPacket, 115);

}
