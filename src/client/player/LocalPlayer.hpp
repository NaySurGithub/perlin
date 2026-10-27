#pragma once
#include "client/Camera.hpp"
#include "client/hand/HandRenderer.hpp"
#include "client/input/InputManager.hpp"
#include "client/multiplayer/BedrockSession.hpp"
#include "client/player/PlayerPhysics.hpp"
#include "client/player/PlayerFov.hpp"
#include "client/renderer/block/BlockLibrary.hpp"

#include <chrono>
#include <deque>
#include <vector>
#include <mutex>
#include <optional>

namespace perlin {
    class LocalPlayer {
    public:
        void Update(BedrockSession& session, InputManager& input, Camera& camera,
                    const BlockLibrary& blocks, float dt);

        const physics::State& State() const { return state; }
        float FovModifier() const { return fov.Sample(PartialTick()); }
        const WalkBob& Bob() const { return walkBob; }
        float PartialTick() const { return static_cast<float>(std::clamp(accumulator / physics::kTickSeconds, 0.0, 1.0)); }
        uint64_t TickCount() const { return tickCounter; }
        size_t PredictionHistorySize() const { return history.size(); }
        using TickObserver = std::function<void(uint64_t, const physics::State&, const physics::Input&)>;
        void ObserveTicks(TickObserver observer) { tickObserver = std::move(observer); }
        bool Sneaking() const { return sneaking; }
        bool Sprinting() const { return sprinting; }
        void SetSwimming(bool value) { swimming = value; }
        bool InWater() const { return state.inWater; }
        bool Swimming() const { return swimming; }
        void SetToggleModes(bool sprint, bool sneak) { sprintToggle = sprint; sneakToggle = sneak; }
        bool SprintLatched() const { return sprintToggle && sprintLatch; }
        bool SneakLatched() const { return sneakToggle && sneakLatch; }
        bool Flying() const { return flying; }
        bool Gliding() const { return gliding; }
        bool Crawling() const { return crawling; }
        void SetElytra(bool value) { hasElytra = value; }
        // restart: a fresh use began this tick. A finite use that completes while
        // still held starts the next one (eating several items in a row).
        void SetItemSlow(bool slow, uint32_t duration = 0, bool restart = false) {
            const bool completed = itemSlowUntil && tickCounter >= itemSlowUntil;
            if (slow && (!itemSlow || restart || completed)) itemSlowUntil = duration ? tickCounter + duration : 0;
            if (!slow || !duration) itemSlowUntil = 0;
            itemSlow = slow;
        }
        void SetRidePosition(bool riding, double x, double y, double z) { rideLock = riding; rideX = x; rideY = y; rideZ = z; }

        void ModMotion(bool add, double x, double y, double z, bool ax, bool ay, bool az);
        void ModTeleport(double x, double y, double z, bool keepVelocity);
        // Replay playback: pin the (otherwise unused) local player to the free camera.
        void ReplayPlace(double px, double py, double pz) {
            state.x = px;
            state.y = py;
            state.z = pz;
            state.vx = state.vy = state.vz = 0;
            state.onGround = false;
        }

        float EyeOffset() const { return eyeOffset; }

    private:
        struct ModCommand {
            bool teleport = false, add = false, keep = false, ax = true, ay = true, az = true;
            double x = 0, y = 0, z = 0;
        };
        std::mutex modMutex;
        std::vector<ModCommand> modQueue;
        void ApplyModCommands();
        void ApplyServerMotion(BedrockSession& session, Camera& camera, BedrockSession::ServerMotion motion);
        void ApplyEffectUpdates(BedrockSession& session);
        void ApplyMotionUpdate(BedrockSession& session, const BedrockSession::MotionUpdate& update);
        void ReplayFrom(BedrockSession& session, size_t first);
        void Tick(BedrockSession& session, const InputManager& input, const Camera& camera,
                  const physics::World& world);

        struct Record {
            uint64_t tick = 0;
            bool simulated = false;
            physics::Input input;
            physics::Attributes attributes;
            float yaw = 0;
            physics::State before;
            physics::State after;
            std::optional<std::array<double, 3>> motion;
            std::optional<BedrockSession::ServerMotion> teleport;
        };
        std::deque<Record> history;
        std::deque<BedrockSession::MotionUpdate> futureMotion;
        std::deque<BedrockSession::ServerMotion> futureTeleports;
        std::optional<uint64_t> teleportMotionBoundary;
        std::optional<int> replayDimension;
        double smoothOffset[3] = {0, 0, 0};

        const BlockLibrary* blocksPtr = nullptr;
        int correctionReplays = 0;
        physics::State state;
        physics::State previous;
        bool initialised = false;
        double accumulator = 0.0;
        WalkBob walkBob;
        uint64_t tickCounter = 0;
        TickObserver tickObserver;

        PlayerFov fov;
        bool flying = false;
        bool serverFlying = false;
        bool sprinting = false;
        bool tooHungryToSprint = false;
        bool swimming = false;
        bool sentSwimming = false;
        bool sprintToggle = false, sneakToggle = false, sprintLatch = false, sneakLatch = false;
        bool gliding = false;
        bool crawling = false;
        bool sentCrawling = false;
        bool sentGliding = false;
        bool hasElytra = false;
        bool sneaking = false;
        bool jumpHeld = false;
        bool sprintHeld = false;
        bool sneakHeld = false;
        bool sendStartFlying = false;
        bool sendStopFlying = false;
        // Client-side prediction bookkeeping: our own flight toggle is trusted until the server answers (or a
        // timeout passes), so a stale ability update can't flip it back. The server still has the final say.
        bool flyPending = false;
        std::chrono::steady_clock::time_point flyPendingAt{};
        bool glideReported = false, glideLocal = false;
        std::chrono::steady_clock::time_point glideChangedAt{};
        bool rideLock = false;
        bool itemSlow = false;
        uint64_t itemSlowUntil = 0;
        double rideX = 0, rideY = 0, rideZ = 0;
        float eyeOffset = static_cast<float>(physics::kStandEye);
        std::chrono::steady_clock::time_point lastJumpPress{};
        std::chrono::steady_clock::time_point lastForwardPress{};
    };
}
