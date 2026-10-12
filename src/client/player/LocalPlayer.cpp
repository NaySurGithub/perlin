#include "client/player/LocalPlayer.hpp"

#include "client/player/BlockCollision.hpp"
#include "protocol/Packets.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "network/custom/PlayerAuthInput.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace perlin {
    namespace {
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

    void LocalPlayer::ApplyServerMotion(BedrockSession& session, Camera& camera) {
        BedrockSession::ServerMotion motion;
        if (!session.TakeServerMotion(motion))
            return;
        bool handled = false;
        if (motion.correction) {
            while (!history.empty() && history.front().tick < motion.tick)
                history.pop_front();
            if (!history.empty() && history.front().tick == motion.tick) {
                Record& r = history.front();
                const double dx = r.after.x - motion.pos[0], dy = r.after.y - motion.pos[1],
                             dz = r.after.z - motion.pos[2];
                // delta is the end-of-tick velocity, the same quantity this client sends.
                const double ex = r.after.vx - motion.velocity[0];
                const double ey = r.after.vy - motion.velocity[1];
                const double ez = r.after.vz - motion.velocity[2];
                const double posErr = dx * dx + dy * dy + dz * dz;
                const double velErr = ex * ex + ey * ey + ez * ez;
                if (posErr > 1e-5 || velErr > 1e-5) {
                    const physics::State shown = state;
                    if (correctionReplays < 40 || correctionReplays % 100 == 0) {
                        // Which blocks the sim stood in when the server disagreed (names, not ids).
                        const Level& lv = session.NetLevel();
                        auto name = [&](int bx, int by, int bz) { return BlockPalette::Instance().Name(lv.GetBlock(bx, by, bz)); };
                        const int fx = static_cast<int>(std::floor(r.before.x)), fz = static_cast<int>(std::floor(r.before.z));
                        const int fy = static_cast<int>(std::floor(r.before.y - 0.001));
                        std::cout << "[move] mismatch tick=" << motion.tick << " ours=(" << r.after.x << "," << r.after.y << ","
                                  << r.after.z << ") server=(" << motion.pos[0] << "," << motion.pos[1] << ","
                                  << motion.pos[2] << ") vel ours=(" << r.after.vx << "," << r.after.vy << "," << r.after.vz
                                  << ") server=(" << motion.velocity[0] << "," << motion.velocity[1] << ","
                                  << motion.velocity[2] << ") ground ours=" << r.after.onGround
                                  << " server=" << motion.onGround << " fwd=" << r.input.forward << " strafe="
                                  << r.input.strafe << " jump=" << r.input.jump << " sprint=" << r.input.sprint
                                  << " sneak=" << r.input.sneak << " feet=" << name(fx, fy + 1, fz) << " under="
                                  << name(fx, fy, fz) << " east=" << name(fx + 1, fy + 1, fz) << " west="
                                  << name(fx - 1, fy + 1, fz) << " north=" << name(fx, fy + 1, fz - 1)
                                  << " south=" << name(fx, fy + 1, fz + 1) << std::endl;
                    }
                    r.after.x = motion.pos[0];
                    r.after.y = motion.pos[1];
                    r.after.z = motion.pos[2];
                    r.after.vx = motion.velocity[0];
                    r.after.vy = motion.velocity[1];
                    r.after.vz = motion.velocity[2];
                    r.after.onGround = motion.onGround;
                    physics::State s = r.after;
                    LevelCollision world(session.NetLevel(), blocksPtr);
                    for (size_t i = 1; i < history.size(); i++) {
                        Record& q = history[i];
                        q.before = s;
                        s.yaw = q.yaw;
                        physics::Step(s, q.input, q.attributes, world);
                        q.after = s;
                    }
                    state = s;
                    const double ox = shown.x - state.x, oy = shown.y - state.y, oz = shown.z - state.z;
                    if (ox * ox + oy * oy + oz * oz < 4.0) {
                        smoothOffset[0] += ox;
                        smoothOffset[1] += oy;
                        smoothOffset[2] += oz;
                    }
                    previous = state;
                    if (!history.empty())
                        previous = history.back().before;
                    session.SetPose(static_cast<float>(state.x), static_cast<float>(state.y),
                                    static_cast<float>(state.z), state.yaw, session.GetPose().pitch);
                    correctionReplays++;
                    if (correctionReplays <= 5 || correctionReplays % 100 == 0)
                        std::cout << "[move] replayed correction tick=" << motion.tick << " posErr=" << std::sqrt(posErr)
                                  << " velErr=" << std::sqrt(velErr) << " replayed=" << (history.size() - 1)
                                  << std::endl;
                }
                history.pop_front();
                handled = true;
                // The correction's velocity was folded into the replay; do not apply it again below.
                motion.hasVelocity = false;
            }
        }
        if (motion.poseChanged && !handled) {
            const BedrockSession::PlayerPose pose = session.GetPose();
            state.x = pose.x;
            state.y = pose.y;
            state.z = pose.z;
            previous = state;
            accumulator = 0.0;
            history.clear();
            if (motion.teleport) {
                state.vx = state.vy = state.vz = 0;
                state.onGround = false;
                smoothOffset[0] = smoothOffset[1] = smoothOffset[2] = 0;
            }
        }
        if (motion.hasVelocity) {
            history.clear();
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
        const physics::State before = state;

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
        in.noClip = flying && (session.NoClip() || session.Spectator());
        in.creativeFlight = session.GameType() == 1;

        physics::Attributes attributes;
        attributes.speed = session.MovementSpeedAttribute();
        attributes.flySpeed = session.FlySpeed();
        attributes.verticalFlySpeed = session.VFlySpeed();

        state.yaw = BedrockYaw(camera.yaw);
        Record rec;
        rec.input = in;
        rec.attributes = attributes;
        rec.yaw = state.yaw;
        rec.before = state;
        if (column) {
            physics::Step(state, in, attributes, world);
        } else {
            state.vx = state.vy = state.vz = 0;
        }

        if (flying && state.onGround && !in.flyUp && !in.noClip) {
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

        // Flag semantics follow the native client (cinnabar encoding::input_flags): the Jump*/Sneak*/Sprint*
        // flags mirror the held keys, the Start*/Stop* edges mirror the processed state.
        const bool jumpInitiated = !flying && before.jumpDelay == 0 && state.jumpDelay == 9;
        if (jump) {
            f.push_back(Flag::JumpDown);
            f.push_back(Flag::Jumping);
            f.push_back(Flag::JumpCurrentRaw);
            f.push_back(Flag::WantUp);
        }
        if (jumpInitiated)
            f.push_back(Flag::StartJumping);
        if (jump && !jumpHeld)
            f.push_back(Flag::JumpPressedRaw);
        if (!jump && jumpHeld)
            f.push_back(Flag::JumpReleasedRaw);
        jumpHeld = jump;

        if (sneakKey) {
            f.push_back(Flag::Sneaking);
            f.push_back(Flag::SneakDown);
            f.push_back(Flag::SneakCurrentRaw);
            f.push_back(Flag::WantDown);
        }
        if (sneaking && !wasSneaking)
            f.push_back(Flag::StartSneaking);
        if (!sneaking && wasSneaking)
            f.push_back(Flag::StopSneaking);
        if (sneakKey && !sneakHeld)
            f.push_back(Flag::SneakPressedRaw);
        if (!sneakKey && sneakHeld)
            f.push_back(Flag::SneakReleasedRaw);
        sneakHeld = sneakKey;

        if (sprintKey || sprinting) {
            f.push_back(Flag::SprintDown);
            f.push_back(Flag::Sprinting);
        }
        if (sprinting && !wasSprinting)
            f.push_back(Flag::StartSprinting);
        if (!sprinting && wasSprinting)
            f.push_back(Flag::StopSprinting);
        sprintHeld = sprintKey;

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

        // move vector: pose-processed (sneak scales the request) and normalised; raw: the keys as pressed.
        const float slowFactor = sneaking ? 0.3f : 1.0f;
        packet.moveX = -std::clamp(strafe, -slowFactor, slowFactor); // Bedrock: +X is left
        packet.moveZ = std::clamp(forward, -slowFactor, slowFactor);
        packet.rawX = -strafe;
        packet.rawZ = forward;
        packet.yaw = BedrockYaw(camera.yaw);
        packet.pitch = BedrockPitch(camera.pitch);
        packet.eyeHeight = kPlayerEyeHeight; // the network position is always feet + 1.62001
        packet.position[0] = static_cast<float>(state.x);
        packet.position[1] = static_cast<float>(state.y);
        packet.position[2] = static_cast<float>(state.z);
        // Vanilla sends the end-of-tick velocity as the position delta.
        packet.delta[0] = static_cast<float>(state.vx);
        packet.delta[1] = static_cast<float>(state.vy);
        packet.delta[2] = static_cast<float>(state.vz);
        packet.hasDelta = true;

        session.SetPose(packet.position[0], packet.position[1], packet.position[2], packet.yaw, packet.pitch);
        if (session.SendMoveInput(packet)) {
            rec.tick = session.LastAuthTick();
            rec.after = state;
            history.push_back(rec);
            while (history.size() > 200)
                history.pop_front();
        }
    }

    void LocalPlayer::Update(BedrockSession& session, const InputManager& input, Camera& camera,
                             const BlockLibrary& blocks, float dt) {
        BedrockSession::PlayerPose pose = session.GetPose();
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
        blocksPtr = &blocks;
        ApplyServerMotion(session, camera);

        const auto now = std::chrono::steady_clock::now();
        const bool spectator = session.NoClip() || session.Spectator();
        if (spectator) {
            if (!flying) {
                flying = true;
                sendStartFlying = true;
            }
        } else if (flying && !session.MayFly()) {
            flying = false;
            sendStopFlying = true;
        }
        // Follow the server when it toggles flight on its own (e.g. /ability or a creative switch).
        if (session.IsFlying() != serverFlying) {
            serverFlying = session.IsFlying();
            if (serverFlying && session.MayFly() && !flying) {
                flying = true;
                state.vy = 0;
            } else if (!serverFlying && flying && !spectator) {
                flying = false;
            }
        }
        if (session.MayFly() && !spectator && input.Pressed(Action::Jump)) {
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
        // The server only simulates us once the spawn handshake is done; until then just hold position.
        if (!session.Initialized()) {
            accumulator = 0.0;
            previous = state;
        } else {
            accumulator += dt;
        }
        int ticks = 0;
        while (accumulator >= physics::kTickSeconds) {
            accumulator -= physics::kTickSeconds;
            if (++ticks > kMaxTicksPerFrame) {
                accumulator = 0.0;
                break;
            }
            Tick(session, input, camera, world);
        }

        {
            float target = 1.0f;
            if (flying)
                target *= 1.1f;
            const float speed = session.MovementSpeedAttribute() * (sprinting ? 1.3f : 1.0f);
            target *= (speed / 0.1f + 1.0f) / 2.0f;
            target = std::clamp(target, 0.1f, 1.5f);
            // Java lerps 0.5 per 20 Hz tick.
            const float keep = std::pow(0.5f, dt * 20.0f);
            fovModifier = target + (fovModifier - target) * keep;
        }

        // Smooth the camera between the last two simulated ticks.
        const double t = std::clamp(accumulator / physics::kTickSeconds, 0.0, 1.0);
        const double x = previous.x + (state.x - previous.x) * t;
        const double y = previous.y + (state.y - previous.y) * t;
        const double z = previous.z + (state.z - previous.z) * t;
        const float targetEye = sneaking ? static_cast<float>(physics::kSneakEye) : static_cast<float>(physics::kStandEye);
        eyeOffset += (targetEye - eyeOffset) * (1.0f - std::exp(-dt * 20.0f));
        const double decay = std::exp(-dt * 12.0);
        for (double& o : smoothOffset)
            o = std::fabs(o) < 1e-4 ? 0.0 : o * decay;
        camera.x = static_cast<float>(x + smoothOffset[0]);
        camera.y = static_cast<float>(y + smoothOffset[1]) + eyeOffset;
        camera.z = static_cast<float>(z + smoothOffset[2]);
    }
}
