#pragma once
#include "client/Camera.hpp"
#include "client/input/InputManager.hpp"
#include "client/multiplayer/BedrockSession.hpp"
#include "client/renderer/block/BlockLibrary.hpp"

#include <chrono>

namespace perlin {
    class LocalPlayer {
    public:
        void Update(BedrockSession& session, const InputManager& input, Camera& camera,
                    const BlockLibrary& blocks, float dt);

    private:
        bool TryPlaceOnTerrain(BedrockSession& session, const Camera& camera);
        bool IsStandable(const BlockLibrary& blocks, uint16_t id) const;
        float GroundTop(Level& level, const BlockLibrary& blocks, float x, float y, float z) const;
        void Fly(BedrockSession& session, const InputManager& input, Camera& camera,
                 const BlockLibrary& blocks, BedrockSession::PlayerPose& pose, float dt, bool sendInput);
        void Walk(BedrockSession& session, const InputManager& input, Camera& camera,
                  const BlockLibrary& blocks, BedrockSession::PlayerPose& pose, float dt, bool sendInput);

        float verticalVelocity = 0;
        bool onGround = false;
        bool flying = false;
        std::chrono::steady_clock::time_point lastAuthInput{};
        std::chrono::steady_clock::time_point lastJumpPress{};
    };
}
