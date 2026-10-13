#pragma once
#include "world/level/Level.hpp"
#include "network/PacketDispatcher.hpp"
#include "protocol/Packets.hpp"
#include "client/multiplayer/SkinLoader.hpp"
#include "network/XboxAuth.hpp"
#include "network/Transport.hpp"
#include "network/PacketCipher.hpp"
#include "client/multiplayer/PackDownloader.hpp"

#include <condition_variable>
#include <deque>
#include <optional>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace perlin {

    struct SubChunkData;
    class ClientIdentity;

    class BedrockSession {
    public:
        enum class State {
            Disconnected,
            Connecting,
            WaitingNetworkSettings,
            WaitingPlayStatus,
            ResourcePacks,
            InGame,
            Failed,
        };

        using PacketHandler = std::function<void(uint32_t id, const std::vector<uint8_t>& body)>;

        BedrockSession();
        ~BedrockSession();

        BedrockSession(const BedrockSession&) = delete;
        BedrockSession& operator=(const BedrockSession&) = delete;

        void SetPlayerName(const std::string& name) { playerName = name.empty() ? std::string("Perlin") : name; }
        // "nethernet", "raknet" or "auto" (NetherNet if the server answers its HTTP probe, otherwise RakNet).
        void SetTransport(const std::string& mode) { transportMode = mode; }
        void SetSkin(LoadedSkin loaded) { skin = std::move(loaded); }
        // Signs in with this Microsoft/Xbox account instead of an offline name (the name becomes the gamertag).
        void SetAccount(std::shared_ptr<xbox::Account> a) { account = std::move(a); }
        void SetChunkRadius(int r) { chunkRadius = r; }
        // Columns farther than this many chunks from the player are fetched only as far-terrain samples (their top
        // sub-chunks, kept just long enough to sample) and promoted to full columns when the player comes closer.
        void SetNearRadius(int r) { nearRadius = r; }
        // Reported to the server in the login (the highest render distance this client can handle).
        void SetMaxViewDistance(int r) { maxViewDistance = r; }

        bool Connect(const std::string& host, uint16_t port);
        void Disconnect();
        // Where server resource packs are kept (empty: do not download any).
        void SetPackDirectory(const std::string& dir) { packs.SetDirectory(dir); }
        // True while the client is moving to another server after a Transfer packet; the session is not usable then.
        bool Transferring() const { return transferring; }

        void Update();

        template <class P> bool Send(const P& packet) { return Transmit(P::kId, net::Encode(packet), true); }
        template <class P> bool SendUnreliable(const P& packet) {
            return Transmit(P::kId, net::Encode(packet), false);
        }

        template <class P, class Fn> void On(Fn&& fn) { dispatcher.On<P>(std::forward<Fn>(fn)); }
        template <class P, class Fn> void OnPartial(Fn&& fn) {
            dispatcher.OnPartial<P>(std::forward<Fn>(fn));
        }

        struct PlayerPose {
            float x = 0, y = 0, z = 0;
            float yaw = 0, pitch = 0, headYaw = 0;
            bool onGround = false;
            bool known = false;
        };
        PlayerPose GetPose() const { return pose; }
        uint64_t GetRuntimeId() const { return runtimeId; }
        bool SendPlayerAuthInput();
        bool SendPlayerAuthInputBlockAction(uint32_t action, int bx, int by, int bz, int face);

        struct BlockActionData {
            uint32_t action = 0;
            int x = 0, y = 0, z = 0, face = 0;
        };
        // One movement tick as the server-authoritative protocol wants it: feet position, the velocity
        // of the tick, the held inputs and the look direction (Bedrock yaw: 0 faces +Z).
        struct MoveInput {
            std::vector<uint32_t> flags;
            float moveX = 0, moveZ = 0; // +X is left, +Z is forward
            float yaw = 0, pitch = 0;
            float eyeHeight = 1.62001f;
            float rawX = 0, rawZ = 0; // unprocessed key vector (left-positive)
            float position[3] = {0, 0, 0};
            float delta[3] = {0, 0, 0};
            bool hasDelta = false;
            bool hasBlockAction = false;
            BlockActionData blockAction;
        };
        bool SendMoveInput(const MoveInput& input);

        // Changes the server forced on us since the last call: teleports, corrections, knockback.
        struct ServerMotion {
            bool poseChanged = false;   // position was replaced
            bool teleport = false;      // MovePlayer teleport/reset or respawn
            bool hasVelocity = false;   // velocity (blocks per tick) was replaced
            bool hasRotation = false;
            float velocity[3] = {0, 0, 0};
            float yaw = 0, pitch = 0;   // Bedrock convention
            // CorrectPlayerMovePrediction: the server's state for input tick `tick` (feet position + tick delta).
            bool correction = false;
            uint64_t tick = 0;
            bool onGround = false;
            double pos[3] = {0, 0, 0};
        };
        uint64_t LastAuthTick() const { return authTick; }
        bool TakeServerMotion(ServerMotion& out);
        float MovementSpeedAttribute() const { return movementSpeed; }
        // True once the spawn handshake finished (loading screen ended, local player initialised).
        bool Initialized() const { return initializedSent; }
        void PruneChunks(int centerChunkX, int centerChunkZ, int radiusChunks);
        bool SendLoadingScreen(bool start, std::optional<uint32_t> id = std::nullopt);
        bool SendPlayerAuthInputWithFlags(const std::vector<uint32_t>& flags, float pitchOverride,
                                          float yawOverride, uint32_t action = UINT32_MAX, int bx = 0,
                                          int by = 0, int bz = 0, int face = 1);
        bool SendChat(const std::string& message);
        bool SendCommand(const std::string& command);
        bool SendTickSync(uint64_t timestamp);
        bool SendPlayerAction(uint32_t action, int bx, int by, int bz, int face = 1);
        void SetPose(float x, float y, float z, float yaw, float pitch);
        bool HaveBlockPos() const { return haveBlockPos; }
        bool GetWorldTime(int64_t& outDay, int& outTicksOfDay) const;
        bool GetWorldTicks(double& outTicks) const;
        void SetDayCycle(bool running);
        int DimensionId() const { return dimensionId; }
        // Increments every time the local player takes damage (drives the red flash and camera shake).
        uint32_t HurtCount() const { return hurtCounter.load(); }
        // Where the latest hit came from: horizontal vector from us toward the source (opposite the knockback).
        struct HurtSource {
            bool known = false;
            float dx = 0, dz = 0;
            uint32_t serial = 0; // bumps when the direction is learned or changes
        };
        HurtSource LatestHurtSource() const {
            std::lock_guard lock(hurtMutex);
            return hurtSource;
        }
        float RainTarget() const { return rainTarget; }
        float ThunderTarget() const { return thunderTarget; }
        void NoteTime(int64_t totalOrTod);
        void NoteClock(int64_t time, bool paused);
        void GetBlockPos(int out[3]) const {
            out[0] = lastBlockPos[0];
            out[1] = lastBlockPos[1];
            out[2] = lastBlockPos[2];
        }
        bool GetSpawn(float out[3]) const {
            if (!pose.known)
                return false;
            out[0] = pose.x;
            out[1] = pose.y;
            out[2] = pose.z;
            return true;
        }

        void SetPacketHandler(PacketHandler h) { onPacket = std::move(h); }

        State GetState() const { return state; }
        std::string LastError() const { return lastError; }
        int ServerProtocol() const { return serverProtocol; }
        bool MayFly() const { return mayFly; }
        bool IsFlying() const { return isFlying; }
        bool Spectator() const { return gameType == 6 || gameType == 3 || gameType == 4 || noClip; }
        bool NoClip() const { return noClip || gameType == 6; }
        int GameType() const { return gameType; }
        float FlySpeed() const { return flySpeed > 0 ? flySpeed : 0.05f; }
        float VFlySpeed() const { return vFlySpeed > 0 ? vFlySpeed : 1.0f; }
        float WalkSpeed() const { return walkSpeed > 0 ? walkSpeed : 0.1f; }
        uint64_t CurrentServerTick() const { return serverTick; }
        bool HaveStartPos() const { return haveStartPos; }
        void GetStartPos(float out[3]) const {
            out[0] = startPos[0];
            out[1] = startPos[1];
            out[2] = startPos[2];
        }
        bool ColumnSettled(int cx, int cz) const;
        bool HaveWorldSpawn() const { return haveWorldSpawn; }
        void GetWorldSpawn(int out[3]) const {
            out[0] = worldSpawn[0];
            out[1] = worldSpawn[1];
            out[2] = worldSpawn[2];
        }

        Level& NetLevel() { return netLevel; }
        bool HasSpawn() const { return gotSpawn; }
        int ChunksReceived() const { return chunksReceived; }
        void PumpRequests(int budget);
        int PendingSubChunkRequests() const;

        static const char* PacketName(uint32_t id);

    private:
        void HandleBatch(const std::vector<uint8_t>& msg);
        void OnGamePacket(uint32_t id, const std::vector<uint8_t>& body);
        void RegisterPacketHandlers();
        net::PacketDispatcher dispatcher;
        bool SendRequestNetworkSettings();
        bool SendLogin();
        bool SendResourcePackResponse(proto::ResourcePackClientResponsePacketResponseStatus status);
        bool SendRequestChunkRadius();
        bool SendClientCacheStatus();
        bool SendSetLocalPlayerAsInitialized();

        bool Transmit(uint32_t packetId, const std::vector<uint8_t>& body, bool reliable);
        void HandleLevelChunk(const proto::LevelChunkPacket& packet);
        void HandleSubChunk(const proto::SubChunkPacket& packet);
        void QueueSubChunkRequests(int dim, int cx, int cz, int firstSubY, int count, bool far = false);
        void UpdateColumnLod();
        // client-side-chunk-generation-enabled=true: the server leaves part of the view for clients to generate
        // themselves. Perlin cannot, so it asks the server for the columns it did not stream.
        void FillUnstreamedColumns();
        bool clientGen = false;       // StartGame.client_side_generation
        int maxViewDistance = 32;
        std::atomic<int> grantedRadius{0};
        std::atomic<int64_t> lastColumnMs{0};
        std::chrono::steady_clock::time_point lastFill{};
        uint64_t statFilled = 0;
        bool ApplySubChunk(int cx, int cz, int absSubY, const SubChunkData& sc);
        void ApplyBiomeColumn(int cx, int cz, const std::vector<uint8_t>& payload);

        // Chunk packets (LevelChunk / SubChunk) are inflated-batch payloads that are expensive to decode; they go to
        // one worker, in order, so a burst of chunk data never stalls a frame.
        void ChunkWorkerMain();
        void StartChunkWorker();
        void StopChunkWorker();
        std::thread chunkThread;
        std::mutex chunkMutex;
        std::condition_variable chunkCv;
        std::deque<std::pair<uint32_t, std::vector<uint8_t>>> chunkIn;
        bool chunkStop = false;
        bool chunkBusy = false;

        bool ConnectImpl(const std::string& host, uint16_t port, bool transfer);
        void BeginTransfer();
        void ResetForTransfer();
        PackDownloader packs;
        size_t packsExpected = 0;
        std::atomic<bool> transferring{false};
        std::atomic<bool> transferRequested{false};
        std::thread transferThread;
        std::thread::id transferThreadId;
        std::mutex updateMutex;
        std::string transferHost;
        uint16_t transferPort = 0;
        bool usedRak = false;

        std::unique_ptr<Transport> net;
        std::unique_ptr<PacketCipher> cipher; // set once the server's handshake switched encryption on
        std::mutex txMutex;                   // keeps encryption counters in step with send order
        std::string transportMode = "auto";
        std::unique_ptr<ClientIdentity> identity;
        std::atomic<State> state{State::Disconnected};
        std::string lastError;
        PacketHandler onPacket;

        Level netLevel;
        std::atomic<bool> gotSpawn{false};
        std::atomic<int> chunksReceived{0};
        std::mutex reqMutex;
        std::set<std::tuple<int, int, int, int>> requestedSlots;
        std::queue<std::tuple<int, int, int, int>> reqQueue;    // near columns: always served first
        std::queue<std::tuple<int, int, int, int>> reqQueueFar; // far-terrain samples
        // What the server announced for every column in range, kept so a far column can be fetched in full later.
        struct ColumnInfo {
            int dim = 0, firstSubY = 0, slots = 0;
            std::vector<uint8_t> biomes;
        };
        std::unordered_map<ChunkPos, ColumnInfo, ChunkPosHash> columns; // reqMutex
        int nearRadius = 32;
        int lodScanCentre[2] = {INT32_MIN, INT32_MIN};
        std::map<std::tuple<int, int, int, int>, std::chrono::steady_clock::time_point> inflight;
        std::map<std::tuple<int, int, int, int>, int> retries;
        void CompleteRequest(const std::tuple<int, int, int, int>& key, bool retry);
        // Forget a requested slot for good (reqMutex held); columns that have nothing left owed are appended.
        void DropSlot(const std::tuple<int, int, int, int>& key, std::vector<std::pair<int, int>>& completed);
        void FinishColumns(const std::vector<std::pair<int, int>>& columns);
        std::map<std::pair<int, int>, int> colPending; // outstanding slots per column

        std::string playerName = "Perlin";
        LoadedSkin skin;
        std::shared_ptr<xbox::Account> account;
        xbox::Credentials onlineCreds;
        std::string playerUuid;
        std::string serverAddress;
        std::string gameVersion;
        PlayerPose pose;
        uint64_t runtimeId = 0;
        int64_t uniqueId = 0;
        uint64_t authTick = 0;
        uint64_t serverTick = 0;
        float lastAuthPos[3] = {0, 0, 0};
        bool haveLastAuthPos = false;
        bool pendingTeleportAck = false;
        std::deque<BlockActionData> pendingBlockActions;
        ServerMotion serverMotion;
        float movementSpeed = 0.1f;
        bool initializedSent = false;
        bool startGameSeen = false;
        std::chrono::steady_clock::time_point startGameAt{};
        std::atomic<int> viewCenterChunk[2] = {0, 0};
        int viewRadiusChunks = 0;
        uint64_t movePacketsSent = 0;
        uint64_t correctionsReceived = 0;
        uint64_t statLevelChunks = 0, statRequestsSent = 0, statSubChunkReplies = 0, statSubChunksApplied = 0,
                 statSubChunksFailed = 0;
        std::chrono::steady_clock::time_point lastStatLog{};
        float startPos[3] = {0, 0, 0};
        bool haveStartPos = false;
        std::set<std::pair<int, int>> seenColumns;
        uint64_t statResult[8] = {};
        void LogChunkStats();
        int lastBlockPos[3] = {0, 0, 0};
        bool haveBlockPos = false;
        int chunkRadius = 8;
        int serverProtocol = 0;

        bool mayFly = false;
        bool noClip = false;
        int gameType = 0;
        bool isFlying = false;
        float flySpeed = 0.05f;
        float vFlySpeed = 1.0f;
        float walkSpeed = 0.1f;

        int worldSpawn[3] = {0, 0, 0};
        bool haveWorldSpawn = false;

        mutable std::mutex timeMutex;
        int64_t timeTotal = -1;
        bool clockPaused = false;
        uint64_t overworldClockId = 0x63d7ede73c38f916ULL;
        std::atomic<int> dimensionId{0};
        std::atomic<uint32_t> hurtCounter{0};
        float lastHealth = -1.0f;
        std::chrono::steady_clock::time_point lastHurtAt{};
        void NoteHurt();
        void NoteKnockback(float vx, float vz);
        mutable std::mutex hurtMutex;
        HurtSource hurtSource;
        float knockX = 0, knockZ = 0;
        std::chrono::steady_clock::time_point knockAt{};
        // A ChangeDimension was received: the world was cleared and the server waits for our acknowledgement.
        std::atomic<bool> dimAckPending{false};
        std::chrono::steady_clock::time_point dimAckSince{};
        std::optional<uint32_t> dimLoadingId;
        void ResetWorldForDimension();
        void FinishDimensionChange();
        std::atomic<float> rainTarget{0.0f};
        std::atomic<float> thunderTarget{0.0f};
        std::atomic<bool> dayCycleRunning{true};
        std::atomic<uint64_t> diagPackets{0};
        std::atomic<uint32_t> diagLastId{0};
        std::chrono::steady_clock::time_point diagAt{};
        uint64_t diagPrevPackets = 0;
        bool diagLostLogged = false;
        std::chrono::steady_clock::time_point timeAt{};

        bool compressEnabled = false;
        uint16_t compressThreshold = 0;
        uint16_t compressAlgorithm = 0;
        bool cacheStatusSent = false;
        bool radiusSent = false;
    };

}
