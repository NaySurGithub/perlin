#include "client/player/LocalPlayer.hpp"
#include "protocol/Packets.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace perlin {
    namespace {
        constexpr float kGravity = 20.0f;
        constexpr float kJumpVelocity = 8.0f;
        constexpr float kWalkSpeedScale = 4.3f / 0.1f;
        constexpr float kFlySpeedScale = 5.0f / 0.05f;
        constexpr float kVerticalFlyScale = 10.0f;
        constexpr float kFallbackSpawnY = 80.0f;
        constexpr auto kAuthInterval = std::chrono::milliseconds(50);
        constexpr auto kDoubleTapWindow = std::chrono::milliseconds(350);

        void Advance(BedrockSession::PlayerPose& pose, float yawDegrees, float forward, float strafe) {
            const float yaw = yawDegrees * 0.01745329252f;
            pose.x += forward * std::cos(yaw) - strafe * std::sin(yaw);
            pose.z += forward * std::sin(yaw) + strafe * std::cos(yaw);
        }

        void Normalize(float& forward, float& strafe, float speed) {
            const float length = std::sqrt(forward * forward + strafe * strafe);
            if (length > 0.0001f) {
                forward = forward / length * speed;
                strafe = strafe / length * speed;
            }
        }

        void AddDirectionFlags(std::vector<uint32_t>& flags, const InputManager& input) {
            if (input.IsDown(Action::Forward))
                flags.push_back(InputFlag::Up);
            if (input.IsDown(Action::Back))
                flags.push_back(InputFlag::Down);
            if (input.IsDown(Action::Left))
                flags.push_back(InputFlag::Left);
            if (input.IsDown(Action::Right))
                flags.push_back(InputFlag::Right);
        }
    }

    bool LocalPlayer::IsStandable(const BlockLibrary& blocks, uint16_t id) const {
        if (id == 0)
            return false;
        const BlockAppearance& look = blocks.Look(static_cast<int32_t>(id));
        if ((look.top | look.side | look.bottom) == 0)
            return true;

        const BlockPalette& palette = BlockPalette::Instance();
        if (look.shape == BlockAppearance::Shape::Flat)
            return palette.HasState(id, "height");
        if (look.shape == BlockAppearance::Shape::Cross || look.shape == BlockAppearance::Shape::Wall)
            return false;
        return !palette.HasState(id, "liquid_depth");
    }

    float LocalPlayer::GroundTop(Level& level, const BlockLibrary& blocks, float x, float y, float z) const {
        const int found = level.FindBlockBelow(
            static_cast<int>(std::floor(x)), static_cast<int>(std::floor(z)), static_cast<int>(std::floor(y)),
            [&](uint16_t id) { return IsStandable(blocks, id); });
        return found < Chunk::MIN_Y ? static_cast<float>(Chunk::MIN_Y) : static_cast<float>(found) + 1.0f;
    }

    bool LocalPlayer::TryPlaceOnTerrain(BedrockSession& session, const Camera& camera) {
        if (!session.HaveStartPos())
            return false;

        float start[3];
        session.GetStartPos(start);
        const int bx = static_cast<int>(std::floor(start[0]));
        const int bz = static_cast<int>(std::floor(start[2]));
        if (!session.ColumnSettled(bx >> 4, bz >> 4))
            return false;

        Level& level = session.NetLevel();
        const int top = level.FindBlockBelow(bx, bz, Chunk::MIN_Y + Chunk::HEIGHT - 1,
                                             [](uint16_t id) { return id != 0; });
        const float feet = top >= Chunk::MIN_Y ? static_cast<float>(top) + 1.0f : kFallbackSpawnY;
        session.SetPose(start[0], feet, start[2], camera.yaw, camera.pitch);
        verticalVelocity = 0;
        std::cout << "[spawn] placed on terrain at (" << start[0] << "," << feet << "," << start[2] << ")"
                  << std::endl;
        return true;
    }

    void LocalPlayer::Fly(BedrockSession& session, const InputManager& input, Camera& camera,
                          const BlockLibrary& blocks, BedrockSession::PlayerPose& pose, float dt,
                          bool sendInput) {
        const float speed = session.FlySpeed() * kFlySpeedScale;
        const float vertical = session.VFlySpeed() * kVerticalFlyScale * dt;

        float forward = input.Axis(Action::Forward, Action::Back);
        float strafe = input.Axis(Action::Right, Action::Left);
        Normalize(forward, strafe, speed * dt);
        Advance(pose, camera.yaw, forward, strafe);

        const bool up = input.IsDown(Action::Jump);
        const bool down = input.IsDown(Action::Sneak);
        if (up)
            pose.y += vertical;
        if (down)
            pose.y -= vertical;

        std::vector<uint32_t> flags;
        AddDirectionFlags(flags, input);
        if (up)
            flags.push_back(InputFlag::WantUp);
        if (down)
            flags.push_back(InputFlag::WantDown);
        flags.push_back(InputFlag::StartFlying);

        const bool loaded = session.ColumnSettled(static_cast<int>(std::floor(pose.x)) >> 4,
                                                  static_cast<int>(std::floor(pose.z)) >> 4);
        if (loaded && down) {
            const float ground = GroundTop(session.NetLevel(), blocks, pose.x, pose.y, pose.z);
            if (pose.y <= ground + 0.001f) {
                pose.y = ground;
                flying = false;
                verticalVelocity = 0;
                onGround = true;
                flags.push_back(InputFlag::StopFlying);
            }
        }

        session.SetPose(pose.x, pose.y, pose.z, camera.yaw, camera.pitch);
        if (sendInput)
            session.SendPlayerAuthInputWithFlags(flags, camera.pitch, camera.yaw);
    }

    void LocalPlayer::Walk(BedrockSession& session, const InputManager& input, Camera& camera,
                           const BlockLibrary& blocks, BedrockSession::PlayerPose& pose, float dt,
                           bool sendInput) {
        float forward = input.Axis(Action::Forward, Action::Back);
        float strafe = input.Axis(Action::Right, Action::Left);
        Normalize(forward, strafe, session.WalkSpeed() * kWalkSpeedScale * dt);
        Advance(pose, camera.yaw, forward, strafe);

        const bool loaded = session.ColumnSettled(static_cast<int>(std::floor(pose.x)) >> 4,
                                                  static_cast<int>(std::floor(pose.z)) >> 4);
        if (loaded) {
            verticalVelocity -= kGravity * dt;
            pose.y += verticalVelocity * dt;
        } else {
            verticalVelocity = 0;
        }

        const float ground = loaded ? GroundTop(session.NetLevel(), blocks, pose.x, pose.y, pose.z) : pose.y;
        if (!loaded || (pose.y <= ground + 0.001f && verticalVelocity <= 0)) {
            pose.y = ground;
            verticalVelocity = 0;
            onGround = true;
        } else {
            onGround = false;
        }

        const bool jumping = onGround && input.IsDown(Action::Jump);
        if (jumping) {
            verticalVelocity = kJumpVelocity;
            onGround = false;
        }

        std::vector<uint32_t> flags;
        AddDirectionFlags(flags, input);
        if (jumping)
            flags.push_back(InputFlag::Jumping);

        session.SetPose(pose.x, pose.y, pose.z, camera.yaw, camera.pitch);
        if (sendInput)
            session.SendPlayerAuthInputWithFlags(flags, camera.pitch, camera.yaw);
    }

    void LocalPlayer::Update(BedrockSession& session, const InputManager& input, Camera& camera,
                             const BlockLibrary& blocks, float dt) {
        BedrockSession::PlayerPose pose = session.GetPose();
        if (!pose.known && TryPlaceOnTerrain(session, camera))
            pose = session.GetPose();
        if (!pose.known)
            return;

        const auto now = std::chrono::steady_clock::now();
        const bool mayFly = session.MayFly();
        if (flying && !mayFly)
            flying = false;
        if (mayFly && input.Pressed(Action::Jump)) {
            if (now - lastJumpPress <= kDoubleTapWindow) {
                flying = !flying;
                verticalVelocity = 0;
                lastJumpPress = {};
                session.SendPlayerAuthInputWithFlags(
                    {flying ? InputFlag::StartFlying : InputFlag::StopFlying}, pose.pitch, pose.yaw);
            } else {
                lastJumpPress = now;
            }
        }

        const bool sendInput = now - lastAuthInput >= kAuthInterval;
        if (sendInput)
            lastAuthInput = now;

        if (flying)
            Fly(session, input, camera, blocks, pose, dt, sendInput);
        else
            Walk(session, input, camera, blocks, pose, dt, sendInput);

        const BedrockSession::PlayerPose updated = session.GetPose();
        if (updated.known) {
            camera.x = updated.x;
            camera.y = updated.y + kPlayerEyeHeight;
            camera.z = updated.z;
        }
    }
}
