#pragma once
#include "client/Camera.hpp"
#include "client/input/InputManager.hpp"
#include "client/multiplayer/BedrockSession.hpp"
#include "client/player/PlayerPhysics.hpp"
#include "client/renderer/block/BlockLibrary.hpp"

#include <chrono>
#include <deque>

namespace perlin {
    // Runs the local player's movement prediction at 20 Hz and reports every tick to the server
    // (server-authoritative movement: the server replays our inputs and corrects us when we diverge).
    class LocalPlayer {
    public:
        void Update(BedrockSession& session, const InputManager& input, Camera& camera,
                    const BlockLibrary& blocks, float dt);

        const physics::State& State() const { return state; }
        // Java's LocalPlayer.getFieldOfViewModifier, smoothed: flying x1.1, speed attribute (sprint +30%) averaged with 1.
        float FovModifier() const { return fovModifier; }

    private:
        void ApplyServerMotion(BedrockSession& session, Camera& camera);
        void Tick(BedrockSession& session, const InputManager& input, const Camera& camera,
                  const physics::World& world);

        // One predicted tick, kept until the server confirms it (cinnabar PredictionHistory).
        struct Record {
            uint64_t tick = 0;
            physics::Input input;
            physics::Attributes attributes;
            float yaw = 0;
            physics::State before; // state the tick started from
            physics::State after;  // state the tick produced
        };
        std::deque<Record> history;
        double smoothOffset[3] = {0, 0, 0}; // render offset that hides small corrections

        const BlockLibrary* blocksPtr = nullptr;
        int correctionReplays = 0;
        physics::State state;
        physics::State previous;
        bool initialised = false;
        double accumulator = 0.0;

        float fovModifier = 1.0f;
        bool flying = false;
        bool serverFlying = false;
        bool sprinting = false;
        bool sneaking = false;
        bool jumpHeld = false;
        bool sprintHeld = false;
        bool sendStartFlying = false;
        bool sendStopFlying = false;
        float eyeOffset = static_cast<float>(physics::kStandEye);
        std::chrono::steady_clock::time_point lastJumpPress{};
        std::chrono::steady_clock::time_point lastForwardPress{};
    };
}
