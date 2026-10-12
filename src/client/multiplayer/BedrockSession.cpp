#include "util/Paths.hpp"
#include "client/multiplayer/BedrockSession.hpp"

#include "network/ClientIdentity.hpp"
#include "network/NetherNet.hpp"
#include "network/VarInt.hpp"
#include "network/LoginClaims.hpp"
#include "network/Uuid.hpp"
#include "protocol/Packets.hpp"
#include "util/Json.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "world/level/chunk/ChunkCodec.hpp"
#include "client/Options.hpp"

#include <miniz.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <mutex>
#include <random>
#include <set>
#include <streambuf>
#include <thread>

namespace perlin {

    using proto::LevelChunkPacket;
    using proto::PlayerAuthInputPacket;
    using proto::SubChunkEntry;
    using proto::SubChunkPacket;
    using proto::SubChunkRequestPacket;
    using proto::SubChunkResult;

    BedrockSession::BedrockSession() {
        RegisterPacketHandlers();
    }
    BedrockSession::~BedrockSession() {
        Disconnect();
    }

    namespace {

        bool RawDeflate(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
            out.clear();
            mz_stream strm = {};
            if (mz_deflateInit2(&strm, MZ_DEFAULT_LEVEL, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9,
                                MZ_DEFAULT_STRATEGY) != MZ_OK)
                return false;
            strm.next_in = in.data();
            strm.avail_in = (unsigned)in.size();
            uint8_t chunk[65536];
            int rc;
            do {
                strm.next_out = chunk;
                strm.avail_out = sizeof(chunk);
                rc = mz_deflate(&strm, MZ_FINISH);
                if (rc != MZ_OK && rc != MZ_STREAM_END) {
                    mz_deflateEnd(&strm);
                    return false;
                }
                out.insert(out.end(), chunk, chunk + (sizeof(chunk) - strm.avail_out));
            } while (rc != MZ_STREAM_END);
            mz_deflateEnd(&strm);
            return true;
        }

        bool RawInflate(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out) {
            out.clear();
            mz_stream strm = {};
            if (mz_inflateInit2(&strm, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK)
                return false;
            strm.next_in = in;
            strm.avail_in = (unsigned)inLen;
            uint8_t chunk[65536];
            int rc;
            do {
                strm.next_out = chunk;
                strm.avail_out = sizeof(chunk);
                rc = mz_inflate(&strm, MZ_SYNC_FLUSH);
                if (rc != MZ_OK && rc != MZ_STREAM_END) {
                    mz_inflateEnd(&strm);
                    return false;
                }
                out.insert(out.end(), chunk, chunk + (sizeof(chunk) - strm.avail_out));
                if (out.size() > 64 * 1024 * 1024) {
                    mz_inflateEnd(&strm);
                    return false;
                }
            } while (rc != MZ_STREAM_END);
            mz_inflateEnd(&strm);
            return true;
        }
    }

    const char* BedrockSession::PacketName(uint32_t id) {
        return proto::PacketName(id);
    }

    namespace {

        constexpr int32_t kCommandVersion = 52;

        std::optional<proto::LoginPacket> BuildOfflineLogin(int protocol, const std::string& playerName,
                                                            const std::string& gameVersion,
                                                            const std::string& serverAddress,
                                                            ClientIdentity& identity, std::string& error) {
            if (!identity.Valid() || identity.MultiplayerToken().empty()) {
                error = "no client identity";
                return std::nullopt;
            }
            const std::string& mpToken = identity.MultiplayerToken();
            std::string cpk = identity.PublicKeyB64();
            if (cpk.empty()) {
                error = "no identity public key";
                return std::nullopt;
            }

            const std::string identityJson =
                json::Write(claims::Identity{json::Write(claims::Chain{}), 2, mpToken});

            auto signJwt = [&](const auto& header, const auto& payload) -> std::string {
                std::string h = B64Url(json::Write(header));
                std::string p = B64Url(json::Write(payload));
                std::vector<uint8_t> raw;
                if (!identity.Sign(h + "." + p, raw))
                    return {};
                return h + "." + p + "." + B64Url(raw.data(), raw.size());
            };

            std::string skinData(64 * 64 * 4, '\0');
            std::random_device rd;
            uint64_t devRand = (static_cast<uint64_t>(rd()) << 32) | rd();
            char devBuf[37], fabBuf[17];
            snprintf(devBuf, sizeof(devBuf), "d42334a7-6b87-34fe-a78b-%012llx",
                     (unsigned long long)(devRand & 0xFFFFFFFFFFFFULL));
            snprintf(fabBuf, sizeof(fabBuf), "%016llx", (unsigned long long)devRand);
            for (char& c : fabBuf)
                c = (char)tolower(c);
            int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
            claims::ClientData skin;
            skin.ClientRandomId = nowMs;
            skin.DeviceId = devBuf;
            skin.GameVersion = gameVersion;
            skin.PlayFabId = fabBuf;
            skin.SelfSignedId = devBuf;
            skin.ServerAddress = serverAddress;
            skin.SkinData = B64Standard(skinData);
            skin.ThirdPartyName = playerName;
            const claims::Header skinHeader{.x5u = cpk};
            std::string skinJwt = signJwt(skinHeader, skin);
            if (skinJwt.empty()) {
                error = "skin JWT failed";
                return std::nullopt;
            }

            proto::LoginPacket login;
            login.protocolVersion = protocol;
            login.tokens.identity = identityJson;
            login.tokens.client = skinJwt;
            return login;
        }

    }

    bool BedrockSession::Connect(const std::string& host, uint16_t port) {
        Disconnect();
        lastError.clear();
        state = State::Connecting;

        net = std::make_unique<NetherNet>();
        identity = std::make_unique<ClientIdentity>();
        if (!identity->Valid()) {
            lastError = "identity keygen failed";
            state = State::Failed;
            return false;
        }
        playerUuid = RandomUuid();
        if (identity->BuildMultiplayerToken(playerName, playerUuid).empty()) {
            lastError = "multiplayer token build failed";
            state = State::Failed;
            return false;
        }
        if (!net->Connect(host, port, *identity)) {
            lastError = "transport: " + net->LastError();
            state = State::Failed;
            return false;
        }
        serverProtocol = net->Info().protocol;
        if (serverProtocol <= 0) {
            lastError = "server reported no protocol version";
            state = State::Failed;
            return false;
        }

        serverAddress = host + ":" + std::to_string(port);
        gameVersion = net->Info().version;
        if (gameVersion.empty())
            gameVersion = "1.26.51";
        if (!SendRequestNetworkSettings()) {
            lastError = "failed to send RequestNetworkSettings";
            state = State::Failed;
            return false;
        }
        state = State::WaitingNetworkSettings;

        auto start = std::chrono::steady_clock::now();
        while (state != State::InGame && state != State::Failed) {
            Update();
            if (std::chrono::steady_clock::now() - start > std::chrono::seconds(60)) {
                lastError = "handshake timed out in state " + std::to_string((int)state.load());
                state = State::Failed;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return state == State::InGame;
    }

    void BedrockSession::Disconnect() {
        state = State::Disconnected;
        if (net)
            net->Disconnect();
        compressEnabled = false;
        cacheStatusSent = false;
        radiusSent = false;
    }

    bool BedrockSession::Transmit(uint32_t packetId, const std::vector<uint8_t>& body, bool reliable) {
        if (!net || !net->IsConnected())
            return false;
        std::vector<uint8_t> content;
        wire::WriteVarUint32(content, static_cast<uint32_t>(body.size() + wire::VarUint32Size(packetId)));
        wire::WriteVarUint32(content, packetId);
        content.insert(content.end(), body.begin(), body.end());

        std::vector<uint8_t> batch;
        if (!compressEnabled) {
            batch = std::move(content);
        } else if (content.size() > compressThreshold) {
            std::vector<uint8_t> deflated;
            if (!RawDeflate(content, deflated))
                return false;
            batch.push_back(0x00);
            batch.insert(batch.end(), deflated.begin(), deflated.end());
        } else {
            batch.push_back(0xFF);
            batch.insert(batch.end(), content.begin(), content.end());
        }
        return reliable ? net->SendReliable(batch) : net->SendUnreliable(batch);
    }

    void BedrockSession::PruneChunks(int centerX, int centerZ, int radius) {
        std::vector<ChunkPos> doomed;
        {
            std::lock_guard<std::mutex> lock(netLevel.mutex);
            for (const auto& entry : netLevel.chunks) {
                const ChunkPos& p = entry.first;
                if (std::abs(p.x - centerX) > radius || std::abs(p.z - centerZ) > radius)
                    doomed.push_back(p);
            }
        }
        if (doomed.empty())
            return;
        for (const ChunkPos& p : doomed)
            netLevel.RemoveChunk(p.x, p.z);
        std::lock_guard<std::mutex> lk(reqMutex);
        for (const ChunkPos& p : doomed) {
            seenColumns.erase({p.x, p.z});
            for (auto it = requestedSlots.begin(); it != requestedSlots.end();)
                it = (std::get<1>(*it) == p.x && std::get<3>(*it) == p.z) ? requestedSlots.erase(it) : std::next(it);
        }
    }

    void BedrockSession::Update() {
        if (startGameSeen && !initializedSent &&
            std::chrono::steady_clock::now() - startGameAt > std::chrono::seconds(8)) {
            // PlayerSpawn never came; finish the handshake anyway rather than staying a ghost.
            std::cout << "[bedrock] no PlayerSpawn after 8s, initialising anyway" << std::endl;
            SendLoadingScreen(false);
            SendSetLocalPlayerAsInitialized();
        }
        if (net && !net->IsConnected() && !diagLostLogged && state != State::Disconnected) {
            diagLostLogged = true;
            std::cout << "[net] CONNECTION LOST (transport closed) state=" << (int)state.load()
                      << " lastError='" << lastError << "'" << std::endl;
        }
        if (!net || !net->IsConnected())
            return;
        {
            auto t = std::chrono::steady_clock::now();
            if (diagAt.time_since_epoch().count() == 0) {
                diagAt = t;
            } else if (t - diagAt >= std::chrono::seconds(5)) {
                diagAt = t;
                uint64_t n = diagPackets;
                bool known;
                {
                    std::lock_guard<std::mutex> lk(timeMutex);
                    known = timeTotal >= 0;
                }
                std::cout << "[net] 5s: " << (n - diagPrevPackets) << " packets (last id "
                          << diagLastId.load() << " " << PacketName(diagLastId)
                          << "), state=" << (int)state.load() << ", time " << (known ? "known" : "UNKNOWN")
                          << ", chunks=" << chunksReceived << std::endl;
                diagPrevPackets = n;
            }
        }
        std::vector<uint8_t> msg;
        while (net->TryReceive(msg)) {
            HandleBatch(msg);
            if (state == State::Failed)
                return;
        }
    }

    void BedrockSession::HandleBatch(const std::vector<uint8_t>& msg) {
        const uint8_t* data = msg.data();
        size_t size = msg.size();
        std::vector<uint8_t> decompressed;
        if (compressEnabled) {
            if (size < 1) {
                lastError = "empty batch";
                state = State::Failed;
                return;
            }
            if (msg[0] == 0x00) {
                if (!RawInflate(data + 1, size - 1, decompressed)) {
                    lastError = "batch raw-inflate failed (size " + std::to_string(size) + ")";
                    state = State::Failed;
                    return;
                }
                data = decompressed.data();
                size = decompressed.size();
            } else if (msg[0] == 0xFF) {
                data = data + 1;
                size = size - 1;
            } else {
                lastError = "bad compressor byte " + std::to_string(msg[0]);
                state = State::Failed;
                return;
            }
        }
        wire::Reader r(data, size);
        while (r.Remaining() > 0) {
            uint32_t len = 0;
            if (!r.ReadVarUint32(len) || r.Remaining() < len) {
                lastError = "truncated batch";
                state = State::Failed;
                return;
            }
            const uint8_t* pktStart = r.Cursor();
            wire::Reader pr(pktStart, len);
            uint32_t id = 0;
            if (!pr.ReadVarUint32(id)) {
                lastError = "truncated packet id";
                state = State::Failed;
                return;
            }
            id &= 0x3FF;
            size_t headerLen = (size_t)(pr.Cursor() - pktStart);
            std::vector<uint8_t> body(pktStart + headerLen, pktStart + len);
            OnGamePacket(id, body);
            r.Skip(len);
            if (state == State::Failed)
                return;
        }
    }

    void BedrockSession::OnGamePacket(uint32_t id, const std::vector<uint8_t>& body) {
        diagPackets++;
        diagLastId = id;
        if (onPacket)
            onPacket(id, body);
        {
            static std::set<uint32_t> seen;
            if (seen.insert(id).second)
                std::cout << "[net] first " << PacketName(id) << " (id " << id << ", " << body.size()
                          << " bytes)" << std::endl;
        }

        dispatcher.Dispatch(id, body.data(), body.size());
    }

    bool BedrockSession::SendRequestNetworkSettings() {
        proto::RequestNetworkSettingsPacket request;
        request.clientProtocol = serverProtocol;
        return Send(request);
    }

    bool BedrockSession::SendLogin() {
        std::string error;
        const auto login =
            BuildOfflineLogin(serverProtocol, playerName, gameVersion, serverAddress, *identity, error);
        if (!login) {
            lastError = "login build failed: " + error;
            return false;
        }
        std::cout << "[bedrock] sending Login (protocol " << serverProtocol << ")" << std::endl;
        return Send(*login);
    }

    bool BedrockSession::SendResourcePackResponse(proto::ResourcePackClientResponsePacketResponseStatus status) {
        using Status = proto::ResourcePackClientResponsePacketResponseStatus;
        const char* name = "resourcepackstackfinished";
        switch (status) {
            case Status::Refused: name = "cancel"; break;
            case Status::SendPacks: name = "downloading"; break;
            case Status::HaveAllPacks: name = "downloadingfinished"; break;
            default: break;
        }
        std::cout << "[bedrock] ResourcePackClientResponse " << static_cast<int>(status) << std::endl;
        proto::ResourcePackClientResponsePacket p;
        p.responseStatus = status;
        p.responseStatusName = name;
        return Send(p);
    }

    bool BedrockSession::SendClientCacheStatus() {
        return Send(proto::ClientCacheStatusPacket{.enabled = false});
    }

    bool BedrockSession::SendRequestChunkRadius() {
        proto::RequestChunkRadiusPacket p;
        p.chunkRadius = chunkRadius;
        p.maxRadius = 8;
        std::cout << "[bedrock] RequestChunkRadius " << chunkRadius << std::endl;
        return Send(p);
    }

    bool BedrockSession::SendSetLocalPlayerAsInitialized() {
        if (runtimeId == 0)
            return false;
        proto::SetLocalPlayerAsInitializedPacket p;
        p.runtimeEntityId = runtimeId;
        initializedSent = true;
        std::cout << "[bedrock] SetLocalPlayerAsInitialized" << std::endl;
        return Send(p);
    }

    bool BedrockSession::SendLoadingScreen(bool start) {
        proto::ServerboundLoadingScreenPacket p;
        p.type = start ? 1 : 2;
        return Send(p);
    }

    bool BedrockSession::SendPlayerAuthInput() {
        return SendPlayerAuthInputWithFlags({}, pose.pitch, pose.yaw);
    }

    bool BedrockSession::SendPlayerAuthInputWithFlags(const std::vector<uint32_t>& flags, float pitchOverride,
                                                      float yawOverride, uint32_t action, int bx, int by,
                                                      int bz, int face) {
        if (!pose.known)
            return false;
        MoveInput in;
        in.flags = flags;
        in.yaw = yawOverride;
        in.pitch = pitchOverride;
        in.position[0] = pose.x;
        in.position[1] = pose.y;
        in.position[2] = pose.z;
        auto has = [&](uint32_t id) { return std::find(flags.begin(), flags.end(), id) != flags.end(); };
        in.moveX = (has(InputFlag::Left) ? 1.0f : 0.0f) - (has(InputFlag::Right) ? 1.0f : 0.0f);
        in.moveZ = (has(InputFlag::Up) ? 1.0f : 0.0f) - (has(InputFlag::Down) ? 1.0f : 0.0f);
        if (haveLastAuthPos) {
            in.delta[0] = pose.x - lastAuthPos[0];
            in.delta[1] = pose.y - lastAuthPos[1];
            in.delta[2] = pose.z - lastAuthPos[2];
        }
        in.hasDelta = true;
        if (action != UINT32_MAX) {
            in.hasBlockAction = true;
            in.blockAction = {action, bx, by, bz, face};
        }
        return SendMoveInput(in);
    }

    bool BedrockSession::SendMoveInput(const MoveInput& input) {
        auto finite = [](float v) { return std::isfinite(v); };
        for (float v : {input.position[0], input.position[1], input.position[2], input.yaw, input.pitch,
                        input.eyeHeight, input.moveX, input.moveZ})
            if (!finite(v))
                return false;
        if (std::fabs(input.position[0]) > 100000.0f || std::fabs(input.position[1]) > 10000.0f ||
            std::fabs(input.position[2]) > 100000.0f)
            return false;

        std::vector<uint32_t> inputFlags;
        inputFlags.reserve(input.flags.size() + 3);
        auto add = [&](uint32_t id) {
            if (std::find(inputFlags.begin(), inputFlags.end(), id) == inputFlags.end())
                inputFlags.push_back(id);
        };
        for (uint32_t id : input.flags)
            if (id <= 65)
                add(id);

        if (pendingTeleportAck) {
            add(InputFlag::HandledTeleport);
            pendingTeleportAck = false;
        }

        // A block action rides along with the next movement tick instead of costing a tick of its own.
        bool hasAction = input.hasBlockAction;
        BlockActionData action = input.blockAction;
        if (!hasAction && pendingBlockAction.valid) {
            hasAction = true;
            action = pendingBlockAction.data;
            pendingBlockAction.valid = false;
        }
        if (hasAction)
            add(InputFlag::PerformBlockActions);

        float mx = input.moveX, mz = input.moveZ;
        const float mlen = std::sqrt(mx * mx + mz * mz);
        if (mlen > 1.0f) {
            mx /= mlen;
            mz /= mlen;
        }

        constexpr float kPi = 3.14159265358979323846f;
        const float yaw = input.yaw * (kPi / 180.0f);
        const float pitch = input.pitch * (kPi / 180.0f);
        const float cp = std::cos(pitch);

        lastAuthPos[0] = input.position[0];
        lastAuthPos[1] = input.position[1];
        lastAuthPos[2] = input.position[2];
        haveLastAuthPos = true;

        PlayerAuthInputPacket f;
        f.pitch = input.pitch;
        f.yaw = input.yaw;
        f.eyeX = input.position[0];
        f.eyeY = input.position[1] + input.eyeHeight;
        f.eyeZ = input.position[2];
        f.moveX = mx;
        f.moveZ = mz;
        f.headYaw = input.yaw;
        f.flags = inputFlags;
        f.interactPitch = input.pitch;
        f.interactYaw = input.yaw;
        f.tick = ++authTick;
        f.deltaX = input.delta[0];
        f.deltaY = input.delta[1];
        f.deltaZ = input.delta[2];
        f.hasBlockAction = hasAction;
        f.blockActionType = static_cast<int32_t>(action.action);
        f.blockX = action.x;
        f.blockY = action.y;
        f.blockZ = action.z;
        f.blockFace = action.face;
        f.analogX = mx;
        f.analogZ = mz;
        f.camX = -std::sin(yaw) * cp;
        f.camY = -std::sin(pitch);
        f.camZ = std::cos(yaw) * cp;
        f.rawX = mx;
        f.rawZ = mz;
        // Sent on the reliable channel: the server only simulates a player when an input arrives, and a
        // dropped input would be a lost tick of movement.
        if (movePacketsSent < 3 || movePacketsSent % 200 == 0)
            std::cout << "[move] input #" << movePacketsSent << " tick=" << f.tick << " eye=(" << f.eyeX << ","
                      << f.eyeY << "," << f.eyeZ << ") move=(" << f.moveX << "," << f.moveZ << ") flags="
                      << f.flags.size() << std::endl;
        movePacketsSent++;
        return Send(f);
    }

    bool BedrockSession::SendPlayerAuthInputBlockAction(uint32_t action, int bx, int by, int bz, int face) {
        if (pendingBlockAction.valid) {
            // Two actions in one tick: flush the older one on its own packet.
            const BlockActionData older = pendingBlockAction.data;
            pendingBlockAction.valid = false;
            SendPlayerAuthInputWithFlags({}, pose.pitch, pose.yaw, older.action, older.x, older.y, older.z,
                                         older.face);
        }
        pendingBlockAction.valid = true;
        pendingBlockAction.data = {action, bx, by, bz, face};
        return true;
    }

    bool BedrockSession::TakeServerMotion(ServerMotion& out) {
        if (!serverMotion.poseChanged && !serverMotion.hasVelocity)
            return false;
        out = serverMotion;
        serverMotion = {};
        return true;
    }

    bool BedrockSession::SendChat(const std::string& message) {
        proto::TextPacket p;
        p.category = proto::TextPacketCategory::Authored;
        p.type = proto::TextPacketType::Chat;
        p.sourceName = playerName;
        p.message = message;
        std::cout << "[bedrock] chat> " << message << std::endl;
        return Send(p);
    }

    bool BedrockSession::SendTickSync(uint64_t timestamp) {
        proto::NetworkStackLatencyPacket p;
        p.timestamp = timestamp;
        return Send(p);
    }

    bool BedrockSession::SendCommand(const std::string& command) {
        proto::CommandRequestPacket p;
        p.command = command;
        p.origin.type = proto::CommandOriginType::Player;
        p.origin.uuid = net::RandomUuidV4();
        p.version = kCommandVersion;
        std::cout << "[bedrock] command> " << command << std::endl;
        return Send(p);
    }

    void BedrockSession::SetPose(float x, float y, float z, float yaw, float pitch) {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(yaw) ||
            !std::isfinite(pitch) || std::fabs(x) > 100000.0f || std::fabs(y) > 10000.0f ||
            std::fabs(z) > 100000.0f)
            return;

        pose.x = x;
        pose.y = y;
        pose.z = z;
        pose.yaw = yaw;
        pose.pitch = pitch;
        pose.headYaw = yaw;
        pose.known = true;
    }

    bool BedrockSession::SendPlayerAction(uint32_t action, int bx, int by, int bz, int face) {
        proto::PlayerActionPacket p;
        p.runtimeEntityId = runtimeId;
        p.action = static_cast<proto::Action>(action);
        p.position = {bx, static_cast<uint32_t>(by), bz};
        p.resultPosition = p.position;
        p.face = face;
        std::cout << "[bedrock] PlayerAction " << action << " at (" << bx << "," << by << "," << bz << ")"
                  << std::endl;
        return Send(p);
    }

    namespace {
        void LogHex(const char* what, const std::string& why, const std::vector<uint8_t>& body) {
            static int budget = 12;
            if (budget-- <= 0)
                return;
            std::cout << "[chunk] " << what << " parse failed: " << why << " (" << body.size()
                      << " bytes) head:";
            for (size_t i = 0; i < body.size() && i < 40; i++) {
                char b[4];
                snprintf(b, sizeof(b), " %02x", body[i]);
                std::cout << b;
            }
            std::cout << std::endl;
        }
    }

    void BedrockSession::HandleLevelChunk(const LevelChunkPacket& pkt) {
        static int logged = 0;
        if (logged < 4) {
            logged++;
            std::cout << "[chunk] LevelChunk (" << pkt.x << "," << pkt.z << ") dim=" << pkt.dimension
                      << " subChunkCount=" << (int32_t)pkt.subChunkCount
                      << " requestMode=" << pkt.hasRequestLimit << " limit=" << pkt.requestLimit
                      << " cache=" << pkt.cacheEnabled << " payload=" << pkt.payload.size() << "B"
                      << std::endl;
        }
        if (pkt.cacheEnabled) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                std::cout << "[chunk] server enabled the blob cache even though we asked for it off; "
                             "chunk data will be missing"
                          << std::endl;
            }
            return;
        }
        if (pkt.dimension != 0) {
            static bool once = false;
            if (!once) {
                once = true;
                std::cout << "[chunk] dimension " << pkt.dimension
                          << " chunks ignored (Level is overworld-only)" << std::endl;
            }
            return;
        }
        const DimensionInfo& dim = ChunkCodec::Dimension(pkt.dimension);
        netLevel.EnsureChunk(pkt.x, pkt.z);
        {
            std::lock_guard<std::mutex> lk(reqMutex);
            seenColumns.insert({pkt.x, pkt.z});
        }
        chunksReceived++;
        statLevelChunks++;

        if (pkt.GetMode() == LevelChunkPacket::Mode::Inline) {
            const uint8_t* p = pkt.payload.data();
            size_t left = pkt.payload.size();
            for (uint32_t i = 0; i < pkt.subChunkCount; i++) {
                SubChunkData sc;
                size_t used = 0;
                const char* why = nullptr;
                if (!ChunkCodec::ParseSubChunk(p, left, sc, used, &why)) {
                    LogHex("inline sub-chunk", why ? why : "?", pkt.payload);
                    break;
                }
                p += used;
                left -= used;
                int absY = sc.hasYIndex ? sc.yIndex : dim.MinSubChunk() + (int)i;
                ApplySubChunk(pkt.x, pkt.z, absY, sc);
            }
        } else {
            ApplyBiomeColumn(pkt.x, pkt.z, pkt.payload);
            QueueSubChunkRequests(pkt.dimension, pkt.x, pkt.z, dim.MinSubChunk(),
                                  pkt.RequestSlots(dim.slots));
        }
    }

    void BedrockSession::ApplyBiomeColumn(int cx, int cz, const std::vector<uint8_t>& payload) {
        const DimensionInfo& dim = ChunkCodec::Dimension(0);
        size_t off = 0;
        int parsed = 0;
        for (int i = 0; i < dim.slots; i++) {
            if (off >= payload.size())
                break;
            if (payload[off] == 0xFF) {
                off += 1;
                continue;
            }
            uint8_t hdr = payload[off++];
            if (!(hdr & 1)) {
                break;
            }
            unsigned bpb = hdr >> 1;
            if (bpb != 0 && bpb != 1 && bpb != 2 && bpb != 3 && bpb != 4 && bpb != 5 && bpb != 6 &&
                bpb != 8 && bpb != 16) {
                break;
            }
            size_t nwords = 0;
            if (bpb != 0) {
                unsigned perWord = 32 / bpb;
                nwords = (4096 + perWord - 1) / perWord;
                if (off + nwords * 4 > payload.size())
                    break;
            }
            std::array<uint16_t, ChunkCodec::kCells> idx;
            if (bpb == 0) {
                idx.fill(0);
            } else {
                const uint8_t* wp = payload.data() + off;
                uint32_t mask = (bpb == 32) ? 0xFFFFFFFFu : ((1u << bpb) - 1);
                unsigned perWord = 32 / bpb;
                for (unsigned pos = 0; pos < (unsigned)ChunkCodec::kCells; pos++) {
                    size_t wi = pos / perWord;
                    const uint8_t* wb = wp + wi * 4;
                    uint32_t w = (uint32_t)wb[0] | ((uint32_t)wb[1] << 8) | ((uint32_t)wb[2] << 16) |
                                 ((uint32_t)wb[3] << 24);
                    idx[pos] = (uint16_t)((w >> ((pos % perWord) * bpb)) & mask);
                }
                off += nwords * 4;
            }
            std::vector<int32_t> pal;
            if (bpb == 0) {
                wire::Reader pr(payload.data() + off, payload.size() - off);
                int32_t v = 0;
                if (!pr.ReadZigzag32(v))
                    break;
                off = (size_t)(pr.Cursor() - payload.data());
                pal.push_back(v);
            } else {
                wire::Reader pr(payload.data() + off, payload.size() - off);
                int32_t cnt = 0;
                if (!pr.ReadZigzag32(cnt) || cnt <= 0 || cnt > 4096)
                    break;
                pal.resize((size_t)cnt);
                bool ok = true;
                for (int32_t k = 0; k < cnt; k++) {
                    if (!pr.ReadZigzag32(pal[(size_t)k])) {
                        ok = false;
                        break;
                    }
                }
                if (!ok)
                    break;
                off = (size_t)(pr.Cursor() - payload.data());
                bool valid = true;
                for (auto v : idx) {
                    if ((size_t)v >= pal.size()) {
                        valid = false;
                        break;
                    }
                }
                if (!valid)
                    continue;
            }
            std::array<uint8_t, ChunkCodec::kCells> cells;
            for (int c = 0; c < ChunkCodec::kCells; c++) {
                int32_t b = pal[idx[(size_t)c]];
                if (b < 0)
                    b = 0;
                if (b > 255)
                    b = 1;
                cells[(size_t)c] = (uint8_t)b;
            }
            netLevel.SetBiomeSubChunk(cx, cz, dim.MinSubChunk() + i, cells.data());
            parsed++;
        }
        static int logged = 0;
        if (logged < 3) {
            logged++;
            std::cout << "[chunk] biome column (" << cx << "," << cz << ") subchunks=" << parsed
                      << " payload=" << payload.size() << "B" << std::endl;
        }
    }

    void BedrockSession::HandleSubChunk(const SubChunkPacket& pkt) {
        statSubChunkReplies++;
        {
            static int lastLayout = -2;
            static bool lastExact = true;
            static int layoutLogs = 0;
            if ((pkt.layout != lastLayout || pkt.exact != lastExact) && layoutLogs++ < 4) {
                lastLayout = pkt.layout;
                lastExact = pkt.exact;
                std::cout << "[chunk] SubChunk entry layout #" << pkt.layout
                          << (pkt.exact ? " matched the whole packet"
                                        : " NOT exact - only the first entry is trusted")
                          << " (" << pkt.entries.size() << " entries)" << std::endl;
            }
        }
        if (pkt.dimension != 0)
            return;
        const DimensionInfo& dim = ChunkCodec::Dimension(pkt.dimension);
        (void)dim;
        for (const SubChunkEntry& e : pkt.entries) {
            int cx = pkt.x + e.dx, absY = pkt.y + e.dy, cz = pkt.z + e.dz;
            auto result = (SubChunkResult)e.result;
            statResult[e.result < 7 ? e.result : 7]++;
            {
                static int shown = 0;
                if (shown++ < 12)
                    std::cout << "[chunk] reply (" << cx << "," << absY << "," << cz
                              << ") result=" << (int)e.result << " payload=" << e.payload.size() << "B"
                              << std::endl;
            }
            CompleteRequest(std::make_tuple(pkt.dimension, cx, absY, cz),
                            result == SubChunkResult::ChunkNotFound);
            if (result == SubChunkResult::SuccessAllAir) {
                static const std::array<uint16_t, ChunkCodec::kCells> air{};
                netLevel.SetSubChunk(cx, cz, absY, air.data());
                continue;
            }
            if (result != SubChunkResult::Success) {
                static int logged = 0;
                if (logged++ < 8)
                    std::cout << "[chunk] SubChunk (" << cx << "," << absY << "," << cz
                              << ") result=" << (int)e.result << std::endl;
                continue;
            }
            SubChunkData sc;
            size_t used = 0;
            const char* why = nullptr;
            if (!ChunkCodec::ParseSubChunk(e.payload.data(), e.payload.size(), sc, used, &why)) {
                LogHex("SubChunk payload", why ? why : "?", e.payload);
                continue;
            }
            ApplySubChunk(cx, cz, absY, sc);
        }
    }

    bool BedrockSession::ApplySubChunk(int cx, int cz, int absSubY, const SubChunkData& sc) {
        if (sc.storages.empty())
            return false;
        if (sc.hasYIndex && sc.yIndex != absSubY) {
            static int mismatches = 0;
            if (mismatches++ < 16)
                std::cout << "[chunk] sub-chunk (" << cx << "," << cz << ") y mismatch: placed at " << absSubY
                          << " but payload says " << static_cast<int>(sc.yIndex) << std::endl;
            absSubY = sc.yIndex;
        }
        const PaletteStorage& st = sc.storages[0];
        ChunkCodec::CellIndices idx;
        if (!ChunkCodec::ExpandIndices(st, idx)) {
            static int logged = 0;
            if (logged++ < 8)
                std::cout << "[chunk] sub-chunk (" << cx << "," << absSubY << "," << cz
                          << ") storage did not expand (bpb=" << st.bitsPerBlock
                          << " palette=" << st.palette.size() << ")" << std::endl;
            return false;
        }
        std::vector<uint16_t> local(st.palette.size());
        for (size_t i = 0; i < local.size(); i++)
            local[i] = BlockPalette::Instance().Intern(st.palette[i]);
        std::array<uint16_t, ChunkCodec::kCells> cells;
        for (int i = 0; i < ChunkCodec::kCells; i++)
            cells[(size_t)i] = local[idx[(size_t)i]];
        netLevel.SetSubChunk(cx, cz, absSubY, cells.data());
        statSubChunksApplied++;
        {
            static int shown = 0;
            if (shown++ < 12)
                std::cout << "[chunk] applied (" << cx << "," << absSubY << "," << cz
                          << ") bpb=" << st.bitsPerBlock << " palette=" << st.palette.size()
                          << " layers=" << sc.storages.size() << std::endl;
        }
        return true;
    }

    void BedrockSession::QueueSubChunkRequests(int dim, int cx, int cz, int firstSubY, int count) {
        std::lock_guard<std::mutex> lk(reqMutex);
        for (int i = 0; i < count; i++) {
            auto key = std::make_tuple(dim, cx, firstSubY + i, cz);
            if (!requestedSlots.insert(key).second)
                continue;
            retries.erase(key);
            reqQueue.push(key);
        }
    }

    bool BedrockSession::ColumnSettled(int cx, int cz) const {
        std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(reqMutex));
        if (!seenColumns.count({cx, cz}))
            return false;
        for (const auto& k : requestedSlots)
            if (std::get<0>(k) == 0 && std::get<1>(k) == cx && std::get<3>(k) == cz)
                return false;
        return true;
    }

    void BedrockSession::NoteTime(int64_t v) {
        std::lock_guard<std::mutex> lk(timeMutex);
        timeTotal = v >= 0 ? v : ((v % 24000) + 24000) % 24000;
        timeAt = std::chrono::steady_clock::now();
    }

    void BedrockSession::NoteClock(int64_t time, bool paused) {
        std::lock_guard<std::mutex> lk(timeMutex);
        timeTotal = time;
        clockPaused = paused;
        timeAt = std::chrono::steady_clock::now();
    }

    void BedrockSession::SetDayCycle(bool running) {
        std::lock_guard<std::mutex> lk(timeMutex);
        const auto now = std::chrono::steady_clock::now();
        if (timeTotal >= 0 && dayCycleRunning && !clockPaused && timeAt.time_since_epoch().count() != 0)
            timeTotal += std::chrono::duration_cast<std::chrono::milliseconds>(now - timeAt).count() / 50;
        timeAt = now;
        dayCycleRunning = running;
    }

    bool BedrockSession::GetWorldTicks(double& outTicks) const {
        std::lock_guard<std::mutex> lk(timeMutex);
        if (timeTotal < 0)
            return false;
        double advance = 0.0;
        if (dayCycleRunning && !clockPaused && timeAt.time_since_epoch().count() != 0)
            advance = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - timeAt).count() / 50.0;
        outTicks = static_cast<double>(timeTotal) + advance;
        return true;
    }

    bool BedrockSession::GetWorldTime(int64_t& outDay, int& outTicksOfDay) const {
        std::lock_guard<std::mutex> lk(timeMutex);
        if (timeTotal < 0)
            return false;
        int64_t adv = dayCycleRunning && !clockPaused ? std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now() - timeAt)
                                                .count() /
                                            50
                                      : 0;
        int64_t total = timeTotal + adv;
        outDay = total / 24000;
        outTicksOfDay = (int)(total % 24000);
        return true;
    }

    void BedrockSession::CompleteRequest(const std::tuple<int, int, int, int>& key, bool retry) {
        std::lock_guard<std::mutex> lk(reqMutex);
        inflight.erase(key);
        if (retry && ++retries[key] <= 3) {
            reqQueue.push(key);
            return;
        }
        retries.erase(key);
        requestedSlots.erase(key);
    }

    int BedrockSession::PendingSubChunkRequests() const {
        std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(reqMutex));
        return (int)reqQueue.size();
    }

    void BedrockSession::PumpRequests(int budget) {
        auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lk(reqMutex);
            for (auto it = inflight.begin(); it != inflight.end();) {
                if (now - it->second < std::chrono::seconds(5)) {
                    ++it;
                    continue;
                }
                auto key = it->first;
                it = inflight.erase(it);
                if (++retries[key] <= 3) {
                    reqQueue.push(key);
                } else {
                    retries.erase(key);
                    requestedSlots.erase(key);
                }
            }
        }
        for (int i = 0; i < budget; i++) {
            int dimension = 0, cx = 0, cz = 0, firstY = 0, count = 0;
            {
                std::lock_guard<std::mutex> lk(reqMutex);
                if (reqQueue.empty())
                    break;
                auto key = reqQueue.front();
                dimension = std::get<0>(key);
                cx = std::get<1>(key);
                firstY = std::get<2>(key);
                cz = std::get<3>(key);
                std::vector<int> ys;
                while (!reqQueue.empty() && ys.size() < 1) {
                    auto k = reqQueue.front();
                    if (std::get<0>(k) != dimension || std::get<1>(k) != cx || std::get<3>(k) != cz)
                        break;
                    ys.push_back(std::get<2>(k));
                    inflight[k] = now;
                    reqQueue.pop();
                }
                count = (int)ys.size();
                for (size_t j = 1; j < ys.size(); j++) {
                    if (ys[j] != ys[j - 1] + 1) {
                        for (size_t k = j; k < ys.size(); k++) {
                            auto rk = std::make_tuple(dimension, cx, ys[k], cz);
                            inflight.erase(rk);
                            reqQueue.push(rk);
                        }
                        count = (int)j;
                        break;
                    }
                }
            }
            const auto req = SubChunkRequestPacket::ForColumn(dimension, cx, cz, firstY, count);
            Send(req);
            statRequestsSent += count;
        }
        LogChunkStats();
    }

    void BedrockSession::LogChunkStats() {
        auto now = std::chrono::steady_clock::now();
        if (now - lastStatLog < std::chrono::seconds(3))
            return;
        lastStatLog = now;
        if (statLevelChunks == 0 && statRequestsSent == 0)
            return;
        std::cout << "[chunk] stats: levelChunks=" << statLevelChunks << " requestsSent=" << statRequestsSent
                  << " replies=" << statSubChunkReplies << " subChunksApplied=" << statSubChunksApplied
                  << " failed=" << statSubChunksFailed << " results[0..6,other]=" << statResult[0] << ","
                  << statResult[1] << "," << statResult[2] << "," << statResult[3] << "," << statResult[4]
                  << "," << statResult[5] << "," << statResult[6] << "," << statResult[7]
                  << " queued=" << reqQueue.size() << " inflight=" << inflight.size()
                  << " blocksKnown=" << BlockPalette::Instance().Count() << std::endl;
    }

}
