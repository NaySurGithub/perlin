#pragma once
#include "world/level/Level.hpp"
#include "network/PacketDispatcher.hpp"
#include "network/PacketStream.hpp"

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

    class NetherNet;
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

        void SetPlayerName(const std::string& name) { playerName = name; }
        void SetChunkRadius(int r) { chunkRadius = r; }

        bool Connect(const std::string& host, uint16_t port);
        void Disconnect();

        void Update();

        bool Send(uint32_t packetId, const std::vector<uint8_t>& body);
        template <class P> bool Send(const P& packet) {
            return Send(net::PacketTraits<P>::kId, net::Encode(packet));
        }
        bool SendUnreliable(uint32_t packetId, const std::vector<uint8_t>& body);

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
        void NoteTime(int64_t totalOrTod);
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
        float FlySpeed() const { return flySpeed > 0 ? flySpeed : 0.05f; }
        float VFlySpeed() const { return vFlySpeed > 0 ? vFlySpeed : 1.0f; }
        float WalkSpeed() const { return walkSpeed > 0 ? walkSpeed : 0.1f; }
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
        bool SendResourcePackResponse(uint8_t status);
        bool SendRequestChunkRadius();
        bool SendClientCacheStatus();
        bool SendSetLocalPlayerAsInitialized();

        void HandleLevelChunk(const std::vector<uint8_t>& body);
        void HandleSubChunk(const std::vector<uint8_t>& body);
        void QueueSubChunkRequests(int dim, int cx, int cz, int firstSubY, int count);
        bool ApplySubChunk(int cx, int cz, int absSubY, const SubChunkData& sc);
        void ApplyBiomeColumn(int cx, int cz, const std::vector<uint8_t>& payload);

        static std::vector<uint8_t> BuildOfflineLogin(int protocol, const std::string& playerName,
                                                      const std::string& playerUuid,
                                                      const std::string& gameVersion,
                                                      const std::string& serverAddress,
                                                      ClientIdentity& identity, std::string& error);

        std::unique_ptr<NetherNet> net;
        std::unique_ptr<ClientIdentity> identity;
        std::atomic<State> state{State::Disconnected};
        std::string lastError;
        PacketHandler onPacket;

        Level netLevel;
        std::atomic<bool> gotSpawn{false};
        std::atomic<int> chunksReceived{0};
        std::mutex reqMutex;
        std::set<std::tuple<int, int, int, int>> requestedSlots;
        std::queue<std::tuple<int, int, int, int>> reqQueue;
        std::map<std::tuple<int, int, int, int>, std::chrono::steady_clock::time_point> inflight;
        std::map<std::tuple<int, int, int, int>, int> retries;
        void CompleteRequest(const std::tuple<int, int, int, int>& key, bool retry);

        std::string playerName = "Perlin";
        std::string playerUuid;
        std::string serverAddress;
        std::string gameVersion;
        PlayerPose pose;
        uint64_t runtimeId = 0;
        uint64_t authTick = 0;
        float lastAuthPos[3] = {0, 0, 0};
        bool haveLastAuthPos = false;
        bool pendingTeleportAck = false;
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
        bool isFlying = false;
        float flySpeed = 0.05f;
        float vFlySpeed = 1.0f;
        float walkSpeed = 0.1f;

        int worldSpawn[3] = {0, 0, 0};
        bool haveWorldSpawn = false;

        mutable std::mutex timeMutex;
        int64_t timeTotal = -1;
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
