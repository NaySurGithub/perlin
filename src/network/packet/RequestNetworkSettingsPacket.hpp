#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct RequestNetworkSettingsPacket {
        int32_t protocol = 0;
    };

    PERLIN_PACKET_ID(RequestNetworkSettingsPacket, 193);

}
