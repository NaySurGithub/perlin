#pragma once
#include "client/Camera.hpp"
#include "client/hand/HandRenderer.hpp"
#include "client/input/InputManager.hpp"
#include "client/multiplayer/BedrockSession.hpp"
#include "client/player/PlayerPhysics.hpp"
#include "client/renderer/block/BlockLibrary.hpp"

#include <chrono>
#include <deque>

namespace perlin {

    class LocalPlayer {
    public:
        void Update(BedrockSession& session, const InputManager& input, Camera& camera,
                    const BlockLibrary& blocks, float dt);

        const physics::State& State() const { return state; }

        float FovModifier() const { return fovModifier; }

        const WalkBob& Bob() const { return walkBob; }
        float PartialTick() const { return static_cast<float>(std::clamp(accumulator / physics::kTickSeconds, 0.0, 1.0)); }
        uint64_t TickCount() const { return tickCounter; }
        bool Sneaking() const { return sneaking; }

    private:
        void ApplyServerMotion(BedrockSession& session, Camera& camera);
        void Tick(BedrockSession& session, const InputManager& input, const Camera& camera,
                  const physics::World& world);

        struct Record {
            uint64_t tick = 0;
            physics::Input input;
            physics::Attributes attributes;
            float yaw = 0;
            physics::State before;
            physics::State after;
        };
        std::deque<Record> history;
        double smoothOffset[3] = {0, 0, 0};

        const BlockLibrary* blocksPtr = nullptr;
        int correctionReplays = 0;
        physics::State state;
        physics::State previous;
        bool initialised = false;
        double accumulator = 0.0;
        WalkBob walkBob;
        uint64_t tickCounter = 0;

        float fovModifier = 1.0f;
        bool flying = false;
        bool serverFlying = false;
        bool sprinting = false;
        bool sneaking = false;
        bool jumpHeld = false;
        bool sprintHeld = false;
        bool sneakHeld = false;
        bool sendStartFlying = false;
        bool sendStopFlying = false;
        float eyeOffset = static_cast<float>(physics::kStandEye);
        std::chrono::steady_clock::time_point lastJumpPress{};
        std::chrono::steady_clock::time_point lastForwardPress{};
    };
}
