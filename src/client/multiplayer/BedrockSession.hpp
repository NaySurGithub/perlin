#pragma once
#include "world/level/Level.hpp"
#include "network/PacketDispatcher.hpp"
#include "protocol/Packets.hpp"
#include "client/multiplayer/SkinLoader.hpp"
#include "client/entity/EntityTracker.hpp"
#include "network/XboxAuth.hpp"
#include "network/Transport.hpp"
#include "network/PacketCipher.hpp"
#include "network/PlayerConstants.hpp"
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

        void SetTransport(const std::string& mode) { transportMode = mode; }
        void SetSkin(LoadedSkin loaded) { skin = std::move(loaded); }
        const LoadedSkin& Skin() const { return skin; }

        entity::EntityTracker& Entities() { return entities; }

        void SetAccount(std::shared_ptr<xbox::Account> a) { account = std::move(a); }
        void SetChunkRadius(int r) { chunkRadius = r; }

        void SetNearRadius(int r) { nearRadius = r; }

        void SetMaxViewDistance(int r) { maxViewDistance = r; }

        bool Connect(const std::string& host, uint16_t port);
        void Disconnect();

        void SetPackDirectory(const std::string& dir) { packs.SetDirectory(dir); }

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

        void EndItemUseOn();
        bool TakeBlockNamesLearned() { return blockNamesLearned.exchange(false); }
        bool ServerAuthoritativeBlockBreaking() const { return serverAuthBreaking.load(); }
        std::atomic<bool> serverAuthBreaking{true};

        struct BlockActionData {
            uint32_t action = 0;
            int x = 0, y = 0, z = 0, face = 0;
        };

        struct MoveInput {
            std::vector<uint32_t> flags;
            float moveX = 0, moveZ = 0;
            float yaw = 0, pitch = 0;
            float eyeHeight = 1.62001f;
            float rawX = 0, rawZ = 0;
            float position[3] = {0, 0, 0};
            float delta[3] = {0, 0, 0};
            bool hasDelta = false;
            bool hasBlockAction = false;
            BlockActionData blockAction;
        };
        bool SendMoveInput(const MoveInput& input);

        struct ServerMotion {
            bool poseChanged = false;
            bool teleport = false;
            bool hasVelocity = false;
            bool hasRotation = false;
            float velocity[3] = {0, 0, 0};
            float yaw = 0, pitch = 0;

            bool correction = false;
            uint64_t tick = 0;
            bool onGround = false;
            double pos[3] = {0, 0, 0};
        };
        uint64_t LastAuthTick() const { return authTick; }
        bool TakeServerMotion(ServerMotion& out);
        float MovementSpeedAttribute() const { return movementSpeed; }

        bool Initialized() const { return initializedSent; }
        void PruneChunks(int centerChunkX, int centerChunkZ, int radiusChunks);
        bool SendLoadingScreen(bool start, std::optional<uint32_t> id = std::nullopt);
        bool SendPlayerAuthInputWithFlags(const std::vector<uint32_t>& flags, float pitchOverride,
                                          float yawOverride, uint32_t action = UINT32_MAX, int bx = 0,
                                          int by = 0, int bz = 0, int face = 1);
        bool SendChat(const std::string& message);
        uint64_t PacketsSent() const { return txPackets; }
        uint64_t PacketsReceived() const { return diagPackets; }

        std::vector<std::string> TakeChat();

        struct SoundRequest {
            enum class Kind { Named, Level, Stop } kind = Kind::Named;
            std::string name;
            float x = 0, y = 0, z = 0;
            float volume = 1, pitch = 1;
            int32_t extra = 0;
            std::string entityType;
            bool baby = false, global = false, stopAll = false;
        };
        std::vector<SoundRequest> TakeSounds();

        struct PendingPlace {
            int x = 0, y = 0, z = 0;
            std::chrono::steady_clock::time_point at{};
            bool active = false;
            int32_t held = 0;
        };
        bool SendCommand(const std::string& command);

        struct TabPlayer {
            std::string uuid, name;
            std::shared_ptr<const entity::SkinData> skin;
        };
        std::vector<TabPlayer> PlayerList() const {
            std::lock_guard<std::mutex> lk(tabMutex);
            return tabPlayers;
        }

        void ReloadChunks();
        bool SendTickSync(uint64_t timestamp);

        struct BlockCrack {
            int x, y, z, stage;
        };
        std::vector<BlockCrack> Cracks();
        bool SendPlayerAction(uint32_t action, int bx, int by, int bz, int face = 1);

void RequestRespawn();

        bool DeathPending() const {
            std::lock_guard<std::mutex> lk(deathMutex);
            return deathPending;
        }
        void ClearDeath() {
            std::lock_guard<std::mutex> lk(deathMutex);
            deathPending = false;
        }
        std::string DeathCause() const {
            std::lock_guard<std::mutex> lk(deathMutex);
            return deathCause;
        }
        void SetPose(float x, float y, float z, float yaw, float pitch);
        bool HaveBlockPos() const { return haveBlockPos; }
        bool GetWorldTime(int64_t& outDay, int& outTicksOfDay) const;
        bool GetWorldTicks(double& outTicks) const;
        void SetDayCycle(bool running);
        int DimensionId() const { return dimensionId; }

        uint32_t HurtCount() const { return hurtCounter.load(); }

        struct HurtSource {
            bool known = false;
            float dx = 0, dz = 0;
            uint32_t serial = 0;
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

        struct ItemStackInfo {
            std::string name;
            int count = 0;
            int aux = 0;
            int damage = 0;
            uint32_t blockRuntimeId = 0;
            bool enchanted = false;
            bool Empty() const { return name.empty() || count <= 0; }
        };
        struct Vitals {
            float health = 20.0f, maxHealth = 20.0f, absorption = 0.0f;
            float hunger = 20.0f, saturation = 5.0f;
            int level = 0;
            float xpProgress = 0.0f;
            int air = 300, maxAir = 300;
            bool poison = false, wither = false, regeneration = false, hungerEffect = false;
            bool known = false;
        };
        struct InventoryView {
            ItemStackInfo hotbar[9];
            ItemStackInfo main[27];
            ItemStackInfo offhand;
            ItemStackInfo armor[4];
            int selected = 0;
        };
        struct TitleText {
            std::string title, subtitle, actionBar;
            std::chrono::steady_clock::time_point titleAt{}, actionAt{};
            int fadeIn = 10, stay = 70, fadeOut = 20;
            uint32_t serial = 0;
        };
        Vitals GetVitals() const;
        InventoryView GetInventory() const;
        TitleText GetTitleText() const;

        bool SelectHotbarSlot(int slot);

        bool SendSwingArm(const char* source = "build");

        bool SendUseItemOnBlock(int bx, int by, int bz, int face, const float click[3], const float eye[3], int use, const int dest[3]);
        bool SendUseItemInAir(const float eye[3]);
        bool SendReleaseItem(const float eye[3]);
        bool SendAttackEntity(uint64_t targetRuntimeId, const float eye[3], const float click[3]);
        bool SendPickBlock(int bx, int by, int bz);

        bool SendDropSelected(int count);

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
        void HandleSubChunk(const proto::SubchunkPacket& packet);
        void QueueSubChunkRequests(int dim, int cx, int cz, int firstSubY, int count, bool far = false);
        void UpdateColumnLod();

        void FillUnstreamedColumns();
        bool clientGen = false;
        int maxViewDistance = 32;
        std::atomic<int> grantedRadius{0};
        std::atomic<int64_t> lastColumnMs{0};
        std::chrono::steady_clock::time_point lastFill{};
        uint64_t statFilled = 0;
        bool ApplySubChunk(int cx, int cz, int absSubY, const SubChunkData& sc);
        void ApplyBiomeColumn(int cx, int cz, const std::vector<uint8_t>& payload);

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
        std::unique_ptr<PacketCipher> cipher;
        std::mutex txMutex;
        std::string transportMode = "auto";
        std::unique_ptr<ClientIdentity> identity;
        std::atomic<State> state{State::Disconnected};
        std::string lastError;
        PacketHandler onPacket;

        Level netLevel;
        std::atomic<bool> blockNamesLearned{false};
        std::atomic<bool> itemUseOnActive{false};
        int itemUseDest[3] = {0, 0, 0};
        std::atomic<bool> haveLoggedInteraction{false};
        mutable std::mutex placeMutex;
        PendingPlace pendingPlace;
        void ConfirmPlace(int x, int y, int z, int32_t runtimeId);
        void NoteUnknownBlock(int32_t networkId);
        std::atomic<bool> gotSpawn{false};
        std::atomic<int> chunksReceived{0};
        std::mutex reqMutex;
        mutable std::mutex tabMutex;
        mutable std::mutex deathMutex;
        std::string deathCause;
        bool deathPending = false;
        std::vector<TabPlayer> tabPlayers;
        struct CrackState {
            int x = 0, y = 0, z = 0;
            double progress = 0.0;
            double perSecond = 0.0;
            std::chrono::steady_clock::time_point since;
        };
        std::mutex crackMutex;
        std::vector<CrackState> crackStates;
        std::set<std::tuple<int, int, int, int>> requestedSlots;
        std::queue<std::tuple<int, int, int, int>> reqQueue;
        std::queue<std::tuple<int, int, int, int>> reqQueueFar;

        struct ColumnInfo {
            int dim = 0, firstSubY = 0, slots = 0;
            std::vector<uint8_t> biomes;
        };
        std::unordered_map<ChunkPos, ColumnInfo, ChunkPosHash> columns;
        int nearRadius = 32;
        int lodScanCentre[2] = {INT32_MIN, INT32_MIN};
        std::map<std::tuple<int, int, int, int>, std::chrono::steady_clock::time_point> inflight;
        std::map<std::tuple<int, int, int, int>, int> retries;
        void CompleteRequest(const std::tuple<int, int, int, int>& key, bool retry);

        void DropSlot(const std::tuple<int, int, int, int>& key, std::vector<std::pair<int, int>>& completed);
        void FinishColumns(const std::vector<std::pair<int, int>>& columns);
        std::map<std::pair<int, int>, int> colPending;

        std::string playerName = "Perlin";
        LoadedSkin skin;
        entity::EntityTracker entities;
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

        std::atomic<bool> dimAckPending{false};
        std::chrono::steady_clock::time_point dimAckSince{};
        std::optional<uint32_t> dimLoadingId;
        void ResetWorldForDimension();
        void FinishDimensionChange();
        std::atomic<float> rainTarget{0.0f};
        std::atomic<float> thunderTarget{0.0f};
        std::atomic<bool> dayCycleRunning{true};
        std::atomic<uint64_t> diagPackets{0};
        std::atomic<uint64_t> txPackets{0};
        std::mutex idCountMutex;
        std::map<uint32_t, uint64_t> idCounts;
        std::atomic<uint32_t> diagLastId{0};
        std::chrono::steady_clock::time_point diagAt{};
        uint64_t diagPrevPackets = 0;
        bool diagLostLogged = false;
        std::chrono::steady_clock::time_point timeAt{};

        void RegisterPlayerStateHandlers();
        void ResetPlayerState();
        mutable std::mutex statusMutex;
        Vitals vitals;
        InventoryView inventory;
        proto::ItemV4 inventoryRaw[36];
        int32_t nextStackRequestId = -1;
        std::map<int, std::string> itemNames;
        TitleText titleText;
        void PushChat(const std::string& line);
        std::mutex chatMutex;
        std::vector<std::string> chatInbox;
        void PushSound(SoundRequest request);
        std::mutex soundMutex;
        std::vector<SoundRequest> soundInbox;

        bool compressEnabled = false;
        uint16_t compressThreshold = 0;
        uint16_t compressAlgorithm = 0;
        bool cacheStatusSent = false;
        bool radiusSent = false;
    };

}
