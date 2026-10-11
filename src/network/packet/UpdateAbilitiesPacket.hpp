#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct AbilityLayer {
        uint16_t type = 0;
        uint32_t abilities = 0;
        uint32_t values = 0;
        float flySpeed = 0;
        float verticalFlySpeed = 0;
        float walkSpeed = 0;
    };

    struct UpdateAbilitiesPacket {
        int64_t targetPlayerId = 0;
        uint8_t playerPermission = 0;
        uint8_t commandPermission = 0;
        std::vector<AbilityLayer> layers;
    };

    PERLIN_PACKET_ID(UpdateAbilitiesPacket, 187);

}
