#include "client/Perlin.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/player/BlockCollision.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "util/Json.hpp"
#include <limits>

namespace perlin {
    struct ControlTickRecord {
        uint64_t tick;
        std::array<double, 3> position, velocity;
        float forward, strafe;
        bool jump, sneak, sprint, onGround, inWater, horizontalCollision, verticalCollision;
    };
    void Perlin::PollControl() {
        if (!control) return;
        input.pauseOnLostFocus = false;
        inputManager.SetControlTick(player.TickCount());
        for (const auto& c : control->TakeCommands()) {
            controlState.commandError.clear();
            if (c.op == "input" || c.op == "release") {
                std::bitset<kActionCount> actions;
                if (c.op == "input")
                    for (const auto& name : c.actions)
                        for (size_t i = 0; i < kActionCount; ++i)
                            if (name == ActionId(static_cast<Action>(i)) || "key." + name == ActionId(static_cast<Action>(i))) actions.set(i);
                inputManager.SetControlActions(actions, c.ticks ? player.TickCount() + c.ticks : std::numeric_limits<uint64_t>::max());
                if (c.op == "release") input.ReleaseAll();
            } else if (c.op == "look") {
                camera.yaw = c.yaw;
                camera.pitch = std::clamp(c.pitch, -89.0f, 89.0f);
            } else if (c.op == "key") {
                if (c.down) input.OnKeyDown(c.key); else input.OnKeyUp(c.key);
            } else if (c.op == "character") {
                input.OnChar(c.key);
            } else if (c.op == "pointer") {
                input.OnMouseMove(c.x, c.y);
            } else if (c.op == "scroll") {
                input.OnMouseWheel(c.y);
            } else if (c.op == "mouse") {
                input.OnMouseMove(c.x, c.y);
                if (c.down) input.OnMouseDown(c.button); else input.OnMouseUp(c.button);
            } else if (c.op == "command" || c.op == "chat") {
                if (!sessionReady || !session || session->GetState() != BedrockSession::State::InGame)
                    controlState.commandError = "Not connected";
                else if (!(c.op == "command" ? session->SendCommand(c.text) : session->SendChat(c.text)))
                    controlState.commandError = "Send failed";
            } else if (c.op == "probe") {
                controlState.hasProbe = true;
                controlState.probePosition = {static_cast<int>(std::floor(c.x)), static_cast<int>(std::floor(c.y)), static_cast<int>(std::floor(c.z))};
            } else if (c.op == "shutdown") {
                platform->isRunning = false;
            }
            controlState.applied = c.id;
        }
    }

    void Perlin::PublishControl() {
        if (!control) return;
        auto& out = controlState;
        const auto& s = player.State();
        out.tick = player.TickCount();
        out.cursorCaptured = input.Captured(); out.textInput = input.TextMode();
        out.position = {s.x, s.y, s.z}; out.velocity = {s.vx, s.vy, s.vz};
        out.onGround = s.onGround;
        out.sprinting = player.Sprinting(); out.sneaking = player.Sneaking();
        out.swimming = player.Swimming(); out.flying = player.Flying();
        out.yaw = camera.yaw; out.pitch = camera.pitch;
        out.connectionState = session ? static_cast<int>(session->GetState()) : 0;
        out.connected = session && session->GetState() == BedrockSession::State::InGame;
        out.initialized = session && session->Initialized();
        out.authTick = session ? session->LastAuthTick() : 0;
        out.corrections = session ? session->CorrectionCount() : 0;
        out.error = session ? session->LastError() : std::string();
        if (session) {
            out.movementSpeed = session->MovementSpeedAttribute();
            out.serverSprinting = session->ServerSprinting();
            const auto attribute = session->MovementAttribute();
            out.movementAttributeJson = attribute ? json::Write(*attribute) : std::string();
            const auto result = session->LastCommandResult();
            out.commandReplies = result.received;
            out.lastCommandSuccess = result.success;
            out.lastCommandOutput = result.output;
        }
        if (out.hasProbe && session && levelRenderer) {
            const auto& p = out.probePosition;
            const auto& level = session->NetLevel();
            const auto id = level.GetBlock(p[0], p[1], p[2], out.probeLoaded);
            out.probeName = BlockPalette::Instance().Name(id);
            out.probeCollision.clear();
            for (const auto& b : LevelCollision(level, &levelRenderer->Blocks()).Query(p[0], p[1], p[2]).boxes)
                out.probeCollision.push_back({b.x0, b.y0, b.z0, b.x1, b.y1, b.z1});
        }
        control->Publish(out);
        if (renderer && control->TakeFrameRequest()) {
            const auto sink = control;
            renderer->RequestFrameCapture([sink](auto w, auto h, auto pitch, const auto* bgra, bool flipY) {
                sink->Frame(w, h, pitch, bgra, flipY);
            });
        }
    }

    void Perlin::TraceControlTick(uint64_t tick, const physics::State& s, const physics::Input& in) {
        if (!control) return;
        control->Trace(json::Write(ControlTickRecord{tick, {s.x, s.y, s.z}, {s.vx, s.vy, s.vz},
            in.forward, in.strafe, in.jump, in.sneak, in.sprint, s.onGround, s.inWater,
            s.horizontalCollision, s.verticalCollision}));
    }
}
