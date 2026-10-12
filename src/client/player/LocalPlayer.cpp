#include "client/player/LocalPlayer.hpp"

#include "client/player/BlockCollision.hpp"
#include "protocol/Packets.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace perlin {
    namespace {
        constexpr float kFallbackSpawnY = 80.0f;
        constexpr int kMaxTicksPerFrame = 5;
        constexpr auto kDoubleTapWindow = std::chrono::milliseconds(350);
        constexpr auto kSprintTapWindow = std::chrono::milliseconds(300);

        // Camera yaw has 0 along +X; Bedrock yaw has 0 along +Z and grows toward -X.
        float BedrockYaw(float cameraYaw) {
            float yaw = std::fmod(cameraYaw - 90.0f, 360.0f);
            if (yaw >= 180.0f)
                yaw -= 360.0f;
            if (yaw < -180.0f)
                yaw += 360.0f;
            return yaw;
        }

        float CameraYaw(float bedrockYaw) {
            return bedrockYaw + 90.0f;
        }

        // Bedrock pitch grows downward; the camera pitch grows upward.
        float BedrockPitch(float cameraPitch) {
            return -cameraPitch;
        }

        namespace Flag {
            constexpr uint32_t JumpDown = 3;
            constexpr uint32_t SprintDown = 4;
            constexpr uint32_t Jumping = 6;
            constexpr uint32_t Sneaking = 8;
            constexpr uint32_t SneakDown = 9;
            constexpr uint32_t UpLeft = 14;
            constexpr uint32_t UpRight = 15;
            constexpr uint32_t WantUp = 16;
            constexpr uint32_t WantDown = 17;
            constexpr uint32_t Sprinting = 20;
            constexpr uint32_t StartSprinting = 25;
            constexpr uint32_t StopSprinting = 26;
            constexpr uint32_t StartSneaking = 27;
            constexpr uint32_t StopSneaking = 28;
            constexpr uint32_t StartJumping = 31;
            constexpr uint32_t HorizontalCollision = 49;
            constexpr uint32_t VerticalCollision = 50;
            constexpr uint32_t DownLeft = 51;
            constexpr uint32_t DownRight = 52;
            constexpr uint32_t JumpReleasedRaw = 59;
            constexpr uint32_t JumpPressedRaw = 60;
            constexpr uint32_t JumpCurrentRaw = 61;
            constexpr uint32_t SneakReleasedRaw = 62;
            constexpr uint32_t SneakPressedRaw = 63;
            constexpr uint32_t SneakCurrentRaw = 64;
        }
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
        session.SetPose(start[0], feet, start[2], BedrockYaw(camera.yaw), BedrockPitch(camera.pitch));
        std::cout << "[spawn] placed on terrain at (" << start[0] << "," << feet << "," << start[2] << ")"
                  << std::endl;
        return true;
    }

    void LocalPlayer::ApplyServerMotion(BedrockSession& session, Camera& camera) {
        BedrockSession::ServerMotion motion;
        if (!session.TakeServerMotion(motion))
            return;
        if (motion.poseChanged) {
            const BedrockSession::PlayerPose pose = session.GetPose();
            state.x = pose.x;
            state.y = pose.y;
            state.z = pose.z;
            previous = state;
            accumulator = 0.0;
            if (motion.teleport) {
                state.vx = state.vy = state.vz = 0;
                state.onGround = false;
            }
        }
        if (motion.hasVelocity) {
            state.vx = motion.velocity[0];
            state.vy = motion.velocity[1];
            state.vz = motion.velocity[2];
        }
        if (motion.hasRotation) {
            camera.yaw = CameraYaw(motion.yaw);
            camera.pitch = std::clamp(BedrockPitch(motion.pitch), -89.0f, 89.0f);
        }
    }

    void LocalPlayer::Tick(BedrockSession& session, const InputManager& input, const Camera& camera,
                           const physics::World& world) {
        previous = state;

        const float forward = input.Axis(Action::Forward, Action::Back);
        const float strafe = input.Axis(Action::Right, Action::Left); // +1 right
        const bool jump = input.IsDown(Action::Jump);
        const bool sneakKey = input.IsDown(Action::Sneak);
        const bool sprintKey = input.IsDown(Action::Sprint);

        // Sprint: hold the sprint key (or double-tap forward) while moving forward and not sneaking.
        const bool wasSprinting = sprinting;
        if (forward <= 0.0f || sneakKey || state.horizontalCollision)
            sprinting = false;
        else if (sprintKey)
            sprinting = true;

        const bool wasSneaking = sneaking;
        sneaking = sneakKey && !flying;

        const bool column = session.ColumnSettled(static_cast<int>(std::floor(state.x)) >> 4,
                                                  static_cast<int>(std::floor(state.z)) >> 4);

        physics::Input in;
        in.forward = forward;
        in.strafe = strafe;
        in.jump = jump;
        in.sneak = sneakKey;
        in.sprint = sprinting;
        in.flying = flying;
        in.flyUp = flying && jump;
        in.flyDown = flying && sneakKey;

        physics::Attributes attributes;
        attributes.speed = session.MovementSpeedAttribute();
        attributes.flySpeed = session.FlySpeed();
        attributes.verticalFlySpeed = session.VFlySpeed();

        state.yaw = BedrockYaw(camera.yaw);
        if (column) {
            physics::Step(state, in, attributes, world);
        } else {
            state.vx = state.vy = state.vz = 0;
        }

        if (flying && state.onGround && !in.flyUp) {
            flying = false;
            sendStopFlying = true;
        }

        // Report the tick.
        BedrockSession::MoveInput packet;
        std::vector<uint32_t>& f = packet.flags;
        const bool goLeft = strafe < 0.0f, goRight = strafe > 0.0f;
        const bool goForward = forward > 0.0f, goBack = forward < 0.0f;
        if (goForward) f.push_back(InputFlag::Up);
        if (goBack) f.push_back(InputFlag::Down);
        if (goLeft) f.push_back(InputFlag::Left);
        if (goRight) f.push_back(InputFlag::Right);
        if (goForward && goLeft) f.push_back(Flag::UpLeft);
        if (goForward && goRight) f.push_back(Flag::UpRight);
        if (goBack && goLeft) f.push_back(Flag::DownLeft);
        if (goBack && goRight) f.push_back(Flag::DownRight);

        if (jump) {
            f.push_back(Flag::JumpDown);
            f.push_back(Flag::Jumping);
            f.push_back(Flag::JumpCurrentRaw);
        }
        if (jump && !jumpHeld) {
            f.push_back(Flag::StartJumping);
            f.push_back(Flag::JumpPressedRaw);
        }
        if (!jump && jumpHeld)
            f.push_back(Flag::JumpReleasedRaw);
        jumpHeld = jump;

        if (sneaking) {
            f.push_back(Flag::Sneaking);
        }
        if (sneakKey) {
            f.push_back(Flag::SneakDown);
            f.push_back(Flag::SneakCurrentRaw);
        }
        if (sneaking && !wasSneaking) {
            f.push_back(Flag::StartSneaking);
            f.push_back(Flag::SneakPressedRaw);
        }
        if (!sneaking && wasSneaking) {
            f.push_back(Flag::StopSneaking);
            f.push_back(Flag::SneakReleasedRaw);
        }

        if (sprintKey || sprinting)
            f.push_back(Flag::SprintDown);
        if (sprinting)
            f.push_back(Flag::Sprinting);
        if (sprinting && !wasSprinting)
            f.push_back(Flag::StartSprinting);
        if (!sprinting && wasSprinting)
            f.push_back(Flag::StopSprinting);
        sprintHeld = sprintKey;

        if (flying && jump)
            f.push_back(Flag::WantUp);
        if (flying && sneakKey)
            f.push_back(Flag::WantDown);
        if (sendStartFlying) {
            f.push_back(InputFlag::StartFlying);
            sendStartFlying = false;
        }
        if (sendStopFlying) {
            f.push_back(InputFlag::StopFlying);
            sendStopFlying = false;
        }
        if (state.horizontalCollision)
            f.push_back(Flag::HorizontalCollision);
        if (state.verticalCollision)
            f.push_back(Flag::VerticalCollision);

        packet.moveX = -strafe; // Bedrock: +X is left
        packet.moveZ = forward;
        packet.yaw = BedrockYaw(camera.yaw);
        packet.pitch = BedrockPitch(camera.pitch);
        packet.eyeHeight = sneaking ? static_cast<float>(physics::kSneakEye) : static_cast<float>(physics::kStandEye);
        packet.position[0] = static_cast<float>(state.x);
        packet.position[1] = static_cast<float>(state.y);
        packet.position[2] = static_cast<float>(state.z);
        packet.delta[0] = static_cast<float>(state.x - previous.x);
        packet.delta[1] = static_cast<float>(state.y - previous.y);
        packet.delta[2] = static_cast<float>(state.z - previous.z);
        packet.hasDelta = true;

        session.SetPose(packet.position[0], packet.position[1], packet.position[2], packet.yaw, packet.pitch);
        session.SendMoveInput(packet);
    }

    void LocalPlayer::Update(BedrockSession& session, const InputManager& input, Camera& camera,
                             const BlockLibrary& blocks, float dt) {
        BedrockSession::PlayerPose pose = session.GetPose();
        if (!pose.known && TryPlaceOnTerrain(session, camera))
            pose = session.GetPose();
        if (!pose.known)
            return;

        if (!initialised) {
            state = {};
            state.x = pose.x;
            state.y = pose.y;
            state.z = pose.z;
            previous = state;
            initialised = true;
            flying = session.IsFlying();
        }
        ApplyServerMotion(session, camera);

        const auto now = std::chrono::steady_clock::now();
        if (flying && !session.MayFly()) {
            flying = false;
            sendStopFlying = true;
        }
        if (session.MayFly() && input.Pressed(Action::Jump)) {
            if (now - lastJumpPress <= kDoubleTapWindow) {
                flying = !flying;
                state.vy = 0;
                (flying ? sendStartFlying : sendStopFlying) = true;
                lastJumpPress = {};
            } else {
                lastJumpPress = now;
            }
        }
        if (input.Pressed(Action::Forward)) {
            if (now - lastForwardPress <= kSprintTapWindow && !sneaking)
                sprinting = true;
            lastForwardPress = now;
        }

        LevelCollision world(session.NetLevel(), &blocks);
        accumulator += dt;
        int ticks = 0;
        while (accumulator >= physics::kTickSeconds) {
            accumulator -= physics::kTickSeconds;
            if (++ticks > kMaxTicksPerFrame) {
                accumulator = 0.0;
                break;
            }
            Tick(session, input, camera, world);
        }

        // Smooth the camera between the last two simulated ticks.
        const double t = std::clamp(accumulator / physics::kTickSeconds, 0.0, 1.0);
        const double x = previous.x + (state.x - previous.x) * t;
        const double y = previous.y + (state.y - previous.y) * t;
        const double z = previous.z + (state.z - previous.z) * t;
        const float targetEye = sneaking ? static_cast<float>(physics::kSneakEye) : static_cast<float>(physics::kStandEye);
        eyeOffset += (targetEye - eyeOffset) * (1.0f - std::exp(-dt * 20.0f));
        camera.x = static_cast<float>(x);
        camera.y = static_cast<float>(y) + eyeOffset;
        camera.z = static_cast<float>(z);
    }
}
