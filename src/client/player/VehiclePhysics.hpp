#pragma once

#include "client/player/PlayerPhysics.hpp"

#include <cstdint>

namespace perlin::physics::vehicle {
    // BoatPaddleComponent: one 20-byte lane per paddle (left, right) then the tick counter at +40
    struct BoatPaddles {
        int32_t rowEnd[2] = {0, 0};
        int32_t rowStart[2] = {0, 0};
        float rowTime[2] = {0, 0};
        float force[2] = {0, 0};
        int64_t tick = 0;
    };

    // minecraft:buoyant (BuoyancyComponent): the values are entity-definition data, not code
    struct Buoyancy {
        float base = 1.0f;
        float bigWaveProbability = 0.03f;
        float bigWaveSpeed = 10.0f;
        // 0 none, 1 waves, 2 bobbing
        uint8_t movementType = 1;
        bool applyGravity = true;
    };

    struct Boat {
        // StateVector: feet centre and posDelta
        float x = 0, y = 0, z = 0;
        float vx = 0, vy = 0, vz = 0;
        // ActorRotation
        float pitch = 0, yaw = 0;
        float width = 1.4f, height = 0.455f;
        bool onGround = false;
        bool horizontalCollision = false, verticalCollision = false;
        // BoatMovementComponent (+0 byte, +4, +8, +12)
        bool uncontrolled = false;
        int32_t submergedTicks = 0;
        float angularVelocity = 0;
        float friction = 1.0f;
        BoatPaddles paddles;
        // BuoyancyComponent +24 (double) and the float request of this tick
        double waveTimer = 0;
        bool atSurface = false, submerged = false;
        bds::RecoveryState recovery;

        Box Bounds() const;
    };

    struct BoatInput {
        // MoveInputComponent +36 / +40: x is left-positive, z forward
        float moveX = 0, moveZ = 0;
        bool controlled = false;
    };

    // PassengerBoatPaddleInputSystem 0x14b372ea0: analog (non-touch) paddle intent from the move vector
    void PaddleIntent(float moveX, float moveZ, float& left, float& right);

    void TickBoat(Boat& boat, const BoatInput& input, const Buoyancy& buoyancy, const World& world);
}
