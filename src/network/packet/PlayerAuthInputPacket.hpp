#pragma once
#include "network/PacketStream.hpp"

namespace perlin {

    constexpr float kPlayerEyeHeight = 1.62f;

    namespace InputFlag {
        constexpr uint32_t Jumping = 6;
        constexpr uint32_t Up = 10;
        constexpr uint32_t Down = 11;
        constexpr uint32_t Left = 12;
        constexpr uint32_t Right = 13;
        constexpr uint32_t WantUp = 16;
        constexpr uint32_t WantDown = 17;
        constexpr uint32_t StartFlying = 42;
        constexpr uint32_t StopFlying = 43;
    }

    constexpr uint32_t kInputFlagPerformBlockActions = 35;
    constexpr uint32_t kInputFlagHandledTeleport = 37;

    struct PlayerAuthInputPacket {
        float pitch = 0, yaw = 0;
        float eyeX = 0, eyeY = 0, eyeZ = 0;
        float moveX = 0, moveZ = 0;
        float headYaw = 0;
        std::vector<uint32_t> flags;
        uint32_t inputMode = 1;
        uint32_t playMode = 0;
        int32_t interactionModel = 1;
        float interactPitch = 0, interactYaw = 0;
        uint64_t tick = 0;
        float deltaX = 0, deltaY = 0, deltaZ = 0;

        bool hasBlockAction = false;
        int32_t blockActionType = 0;
        int32_t blockX = 0, blockY = 0, blockZ = 0;
        int32_t blockFace = 0;

        float analogX = 0, analogZ = 0;
        float camX = 0, camY = 0, camZ = 1;
        float rawX = 0, rawZ = 0;
    };

}

namespace perlin::net {

    using PlayerAuthInputPacket = perlin::PlayerAuthInputPacket;

    PERLIN_PACKET_ID(PlayerAuthInputPacket, 144);

}
