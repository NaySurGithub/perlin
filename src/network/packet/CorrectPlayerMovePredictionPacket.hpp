#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct CorrectPlayerMovePredictionPacket {
        uint8_t predictionType = 0;
        Vec3 position;
        Vec3 delta;
        float rotationX = 0;
        float rotationY = 0;
        float vehicleAngularVelocity = 0;
        bool onGround = false;
        uint64_t tick = 0;
    };

    PERLIN_PACKET_ID(CorrectPlayerMovePredictionPacket, 161);

}
