#include "client/player/LocalPlayer.hpp"

#include "client/player/BlockCollision.hpp"
#include "protocol/Packets.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "network/PlayerConstants.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>

namespace perlin {
    namespace {
        constexpr int kMaxTicksPerFrame = 5;
        constexpr auto kDoubleTapWindow = std::chrono::milliseconds(350);
        constexpr auto kSprintTapWindow = std::chrono::milliseconds(300);

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
            constexpr uint32_t StartSwimming = 29;
            constexpr uint32_t StopSwimming = 30;
            constexpr uint32_t StartSneaking = 27;
            constexpr uint32_t StopSneaking = 28;
            constexpr uint32_t StartJumping = 31;
            constexpr uint32_t StartGliding = 32;
            constexpr uint32_t StopGliding = 33;
            constexpr uint32_t StartCrawling = 40;
            constexpr uint32_t StopCrawling = 41;
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
                const double ex = r.after.vx - motion.velocity[0];
                const double ey = r.after.vy - motion.velocity[1];
                const double ez = r.after.vz - motion.velocity[2];
                const double posErr = dx * dx + dy * dy + dz * dz;
                const double velErr = ex * ex + ey * ey + ez * ez;
                if (posErr > 1e-5 || velErr > 1e-5) {
                    const physics::State shown = state;
                    if (correctionReplays < 40 || correctionReplays % 100 == 0) {
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

        const float useScale = itemSlow ? 0.2f : 1.0f;
        // While mounted our own body stays on the seat, but the keys still have to reach the server: it steers the
        // saddled horse/pig/strider/boat from the rider's move input and look direction.
        const bool asleep = session.Sleeping();
        const float forwardKeys = asleep ? 0.0f : input.Axis(Action::Forward, Action::Back);
        const float strafeKeys = asleep ? 0.0f : input.Axis(Action::Right, Action::Left);
        const float forward = forwardKeys * useScale;
        const float strafe = strafeKeys * useScale;
        const bool jump = !asleep && input.IsDown(Action::Jump);
        const bool sneakHeldKey = input.IsDown(Action::Sneak);
        const bool sneakKey = sneakToggle ? sneakLatch : sneakHeldKey;
        if (rideLock && sneakKey && !sneakHeld)
            session.SendLeaveVehicle();
        const bool sprintHeldKey = input.IsDown(Action::Sprint);
        const bool sprintKey = sprintToggle ? sprintLatch : sprintHeldKey;

        const bool wasSprinting = sprinting;
        const BedrockSession::Vitals vitals = session.GetVitals();
        tooHungryToSprint = vitals.known && vitals.hunger <= 6.0f;
        if (forward <= 0.0f || sneakKey || itemSlow || state.horizontalCollision || tooHungryToSprint)
            sprinting = false;
        else if (sprintKey)
            sprinting = true;

        const bool wasSneaking = sneaking;
        bool forcedSneak = false;
        bool wantCrawl = false;
        if (!flying && !rideLock && !gliding && !swimming) {
            if (!physics::PoseFits(state, world, physics::kStandHeight)) {
                if (physics::PoseFits(state, world, physics::kSneakHeight))
                    forcedSneak = true;
                else
                    // Ordinary wall/support overlap must not enter a new low
                    // pose. Only retain an existing crawl until
                    // there is enough room to stand again.
                    wantCrawl = crawling || state.crawling;
            }
        }
        crawling = wantCrawl;
        sneaking = (sneakKey || forcedSneak) && !flying;
        if (crawling) {
            sprinting = false;
            sneaking = true;
        }

        const bool column = session.ColumnSettled(static_cast<int>(std::floor(state.x)) >> 4,
                                                  static_cast<int>(std::floor(state.z)) >> 4);

        bool levitating = false;
        for (const auto& effect : session.GetEffects())
            if (effect.id == 24)
                levitating = true;
        swimming = sprinting && !flying && (swimming ? state.inWater : headUnderwater);
        if (gliding && (!hasElytra || state.onGround || state.inWater || flying || rideLock))
            gliding = false;
        else if (!gliding && hasElytra && jump && !jumpHeld && !state.onGround && !state.inWater && !state.inLava &&
                 !state.onClimbable && !flying && !rideLock && !levitating && !crawling)
            gliding = true;
        {
            // Predict gliding locally, but let the server's own-player Gliding flag correct us once it has shown
            // that it reports it (some servers never do, so we don't cancel glides before that).
            const auto glideNow = std::chrono::steady_clock::now();
            const bool serverGlide = session.ServerGliding();
            if (serverGlide)
                glideReported = true;
            if (gliding != glideLocal) {
                glideLocal = gliding;
                glideChangedAt = glideNow;
            }
            if (glideReported && glideNow - glideChangedAt > std::chrono::milliseconds(600)) {
                if (gliding && !serverGlide) {
                    gliding = false;
                    glideLocal = false;
                    glideChangedAt = glideNow;
                } else if (!gliding && serverGlide && hasElytra && !state.onGround && !state.inWater && !flying && !rideLock) {
                    gliding = true;
                    glideLocal = true;
                    glideChangedAt = glideNow;
                }
            }
        }
        physics::Input in;
        in.gliding = gliding;
        in.crawl = crawling;
        in.forceCrouch = forcedSneak;
        in.pitch = BedrockPitch(camera.pitch);
        in.swimming = swimming && !gliding;
        in.headInWater = headUnderwater;
        in.lookY = std::sin(glm::radians(camera.pitch));
        const physics::MoveVector move = physics::ProcessMoveInput(forward, strafe, sneaking, state.inWater);
        in.forward = move.forward;
        in.strafe = move.strafe;
        in.jump = jump;
        in.sneak = sneakKey;
        in.sprint = sprinting;
        in.flying = flying;
        in.flyUp = flying && jump;
        in.flyDown = flying && sneakKey;
        in.noClip = flying && (session.NoClip() || session.Spectator());
        in.creativeFlight = session.GameType() == 1;
        const auto actorFlags = session.OwnActorFlags();
        in.actorMovementFlags = static_cast<uint8_t>(actorFlags[0]);
        in.pushTowardsClosestSpace = (actorFlags[1] & proto::FlagsMetadataFlags2::PushTowardsClosestSpace) != 0;
        in.gravity.hasGravity = (actorFlags[0] & proto::FlagsMetadataFlags1::AffectedByGravity) != 0;
        in.gravity.inScaffolding = (actorFlags[1] & proto::FlagsMetadataFlags2::InScaffolding) != 0;
        in.gravity.overScaffolding = (actorFlags[1] & proto::FlagsMetadataFlags2::OverScaffolding) != 0;
        in.gravity.descendThroughBlock = (actorFlags[1] & proto::FlagsMetadataFlags2::FallThroughScaffolding) != 0;

        physics::Attributes attributes;
        attributes.speed = session.MovementSpeedAttribute();
        attributes.flySpeed = session.FlySpeed();
        attributes.verticalFlySpeed = session.VFlySpeed();
        attributes.waterWalkSpeedEnchant = session.WaterWalkSpeedEnchant();
        attributes.underwaterSpeed = session.UnderwaterMovementSpeedAttribute();
        for (const auto& effect : session.GetEffects()) {
            if (effect.id == 8)
                attributes.jumpBoost = effect.amplifier + 1;
            else if (effect.id == 24)
                attributes.levitation = effect.amplifier + 1;
            else if (effect.id == 27)
                attributes.slowFalling = true;
        }

        if (column && !asleep)
            for (const auto& effect : session.TakeMovementEffects())
                in.movementEffects.push_back({effect.tick, std::bit_cast<int32_t>(effect.duration),
                                              static_cast<int32_t>(effect.type)});

        state.yaw = BedrockYaw(camera.yaw);
        Record rec;
        rec.input = in;
        rec.attributes = attributes;
        rec.yaw = state.yaw;
        rec.before = state;
        if (column && !asleep) {
            physics::Step(state, in, attributes, world);
        } else {
            state.vx = state.vy = state.vz = 0;
        }

        if (rideLock) {
            state.x = rideX;
            state.y = rideY;
            state.z = rideZ;
            state.vx = state.vy = state.vz = 0;
            state.onGround = true;
            flying = false;
        }
        walkBob.Tick(std::hypot(state.x - before.x, state.z - before.z), state.onGround && !flying,
                     state.inWater && sprinting);
        ++tickCounter;

        if (flying && state.onGround && state.verticalCollision && !in.flyUp && !in.noClip) {
            flying = false;
            sendStopFlying = true;
            flyPending = true;
            flyPendingAt = std::chrono::steady_clock::now();
        }

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
        if (swimming && !sentSwimming)
            f.push_back(Flag::StartSwimming);
        if (!swimming && sentSwimming)
            f.push_back(Flag::StopSwimming);
        sentSwimming = swimming;
        if (crawling && !sentCrawling)
            f.push_back(Flag::StartCrawling);
        if (!crawling && sentCrawling)
            f.push_back(Flag::StopCrawling);
        sentCrawling = crawling;
        if (gliding && !sentGliding)
            f.push_back(Flag::StartGliding);
        if (!gliding && sentGliding)
            f.push_back(Flag::StopGliding);
        sentGliding = gliding;

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

        packet.moveX = -move.strafe;
        packet.moveZ = move.forward;
        packet.rawX = -strafeKeys;
        packet.rawZ = forwardKeys;
        packet.yaw = BedrockYaw(camera.yaw);
        packet.pitch = BedrockPitch(camera.pitch);
        packet.eyeHeight = kPlayerEyeHeight;
        packet.position[0] = static_cast<float>(state.x);
        packet.position[1] = static_cast<float>(state.y);
        packet.position[2] = static_cast<float>(state.z);
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

    void LocalPlayer::ModMotion(bool add, double x, double y, double z, bool ax, bool ay, bool az) {
        std::lock_guard lock(modMutex);
        if (modQueue.size() < 256)
            modQueue.push_back({false, add, false, ax, ay, az, x, y, z});
    }

    void LocalPlayer::ModTeleport(double x, double y, double z, bool keepVelocity) {
        std::lock_guard lock(modMutex);
        if (modQueue.size() < 256)
            modQueue.push_back({true, false, keepVelocity, true, true, true, x, y, z});
    }

    void LocalPlayer::ApplyModCommands() {
        std::vector<ModCommand> list;
        {
            std::lock_guard lock(modMutex);
            list.swap(modQueue);
        }
        for (const ModCommand& c : list) {
            if (c.teleport) {
                state.x = c.x;
                state.y = c.y;
                state.z = c.z;
                state.hasBox = false;
                if (!c.keep)
                    state.vx = state.vy = state.vz = 0.0;
                previous = state;
                history.clear();
                continue;
            }
            if (c.ax)
                state.vx = c.add ? state.vx + c.x : c.x;
            if (c.ay)
                state.vy = c.add ? state.vy + c.y : c.y;
            if (c.az)
                state.vz = c.add ? state.vz + c.z : c.z;
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
        if (sneakToggle) {
            if (input.Pressed(Action::Sneak))
                sneakLatch = !sneakLatch;
        } else {
            sneakLatch = false;
        }
        if (sprintToggle) {
            if (input.Pressed(Action::Sprint))
                sprintLatch = !sprintLatch;
        } else {
            sprintLatch = false;
        }
        if (rideLock) {
            BedrockSession::ServerMotion ignored;
            while (session.TakeServerMotion(ignored)) {}
            history.clear();
        } else {
            ApplyServerMotion(session, camera);
        }
        ApplyModCommands();

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
        if (flyPending && (session.IsFlying() == flying || now - flyPendingAt > std::chrono::milliseconds(750)))
            flyPending = false;
        serverFlying = session.IsFlying();
        if (!flyPending && serverFlying != flying) {
            // While our own toggle is still unanswered, an incoming ability update may just be the answer to an
            // earlier toggle; don't let it undo what the player did since.
            if (!flyPending) {
                if (serverFlying && session.MayFly() && !flying) {
                    flying = true;
                } else if (!serverFlying && flying && !spectator) {
                    flying = false;
                }
            }
        }
        if (session.MayFly() && !spectator && input.Pressed(Action::Jump)) {
            if (now - lastJumpPress <= kDoubleTapWindow) {
                flying = !flying;
                (flying ? sendStartFlying : sendStopFlying) = true;
                flyPending = true;
                flyPendingAt = now;
                lastJumpPress = {};
            } else {
                lastJumpPress = now;
            }
        }
        if (input.Pressed(Action::Forward)) {
            if (now - lastForwardPress <= kSprintTapWindow && !sneaking && !tooHungryToSprint)
                sprinting = true;
            lastForwardPress = now;
        }

        LevelCollision world(session.NetLevel(), &blocks);
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
            const float keep = std::pow(0.5f, dt * 20.0f);
            fovModifier = target + (fovModifier - target) * keep;
        }

        const double t = std::clamp(accumulator / physics::kTickSeconds, 0.0, 1.0);
        const double x = previous.x + (state.x - previous.x) * t;
        const double y = previous.y + (state.y - previous.y) * t;
        const double z = previous.z + (state.z - previous.z) * t;
        const float targetEye = session.Sleeping() ? 0.35f : rideLock ? 1.2f : (crawling || gliding || swimming) ? static_cast<float>(physics::kCrawlEye) : sneaking ? static_cast<float>(physics::kSneakEye) : static_cast<float>(physics::kStandEye);
        eyeOffset += (targetEye - eyeOffset) * (1.0f - std::exp(-dt * 20.0f));
        const double offsetSize = std::sqrt(smoothOffset[0] * smoothOffset[0] + smoothOffset[1] * smoothOffset[1] +
                                            smoothOffset[2] * smoothOffset[2]);
        const double decay = std::exp(-dt * (offsetSize < 0.4 ? 7.0 : 14.0));
        for (double& o : smoothOffset)
            o = std::fabs(o) < 1e-4 ? 0.0 : o * decay;
        camera.x = static_cast<float>(x + smoothOffset[0]);
        camera.y = static_cast<float>(y + smoothOffset[1]) + eyeOffset;
        camera.z = static_cast<float>(z + smoothOffset[2]);
    }
}
