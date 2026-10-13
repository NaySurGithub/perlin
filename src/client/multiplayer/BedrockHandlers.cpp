#include "client/multiplayer/BedrockSession.hpp"
#include "network/ClientIdentity.hpp"
#include "network/PacketDump.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace perlin {
    using namespace proto;

    namespace {
        using PackStatus = ResourcePackClientResponsePacketResponseStatus;

        bool SaneWorldPos(float x, float y, float z) {
            return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::fabs(x) < 100000.0f &&
                   std::fabs(y) < 10000.0f && std::fabs(z) < 100000.0f;
        }

        template <class P> void Trace(BedrockSession& session) {
            session.OnPartial<P>([](const P& packet) {
                std::cout << "[trace] " << P::kName << ' ' << net::Dump(packet) << std::endl;
            });
        }

        bool EqualsIgnoreCase(const std::string& a, std::string_view b) {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); i++)
                if (std::tolower(static_cast<unsigned char>(a[i])) !=
                    std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            return true;
        }
    }

    void BedrockSession::RegisterPacketHandlers() {
        dispatcher.OnFailure([](uint32_t id, const char* name, size_t size) {
            std::cout << "[net] failed to decode " << name << " (id " << id << ", " << size << " bytes)"
                      << std::endl;
        });

        On<NetworkSettingsPacket>([this](const NetworkSettingsPacket& p) {
            compressThreshold = p.compressionThreshold;
            compressAlgorithm = static_cast<uint16_t>(p.compressionAlgorithm);
            compressEnabled = true;
            std::cout << "[bedrock] NetworkSettings threshold=" << p.compressionThreshold
                      << " algorithm=" << compressAlgorithm << std::endl;
            if (!SendLogin()) {
                lastError = "failed to send Login";
                state = State::Failed;
            } else {
                state = State::WaitingPlayStatus;
            }
        });

        On<PlayStatusPacket>([this](const PlayStatusPacket& p) {
            std::cout << "[bedrock] PlayStatus " << static_cast<int>(p.status) << std::endl;
            if (state == State::WaitingPlayStatus) {
                if (p.status != PlayStatusPacketStatus::LoginSuccess) {
                    const int code = static_cast<int>(p.status);
                    lastError = "login rejected, PlayStatus=" + std::to_string(code);
                    if (code == 1 || code == 2)
                        lastError += " (the server runs a different Minecraft version than Perlin's " +
                                     std::string(kBedrockVersion) + ")";
                    state = State::Failed;
                }
            } else if (p.status == PlayStatusPacketStatus::PlayerSpawn) {
                // Same order as the vanilla client: end the loading screen, then report ready.
                SendLoadingScreen(false);
                SendSetLocalPlayerAsInitialized();
                state = State::InGame;
                std::cout << "[bedrock] spawned -> InGame" << std::endl;
            }
        });

        On<DisconnectPacket>([this](const DisconnectPacket& p) {
            lastError = "server disconnect, reason=" + std::to_string(static_cast<int>(p.reason)) +
                        (p.message.empty() ? "" : " message='" + p.message + "'");
            switch (static_cast<int>(p.reason)) {
                case 44:
                    lastError += " (ServerIdConflict: the server still has a session with this name/id from an earlier"
                                 " run. Wait ~30 s, restart the server, or join with a different --name)";
                    break;
                case 35:
                case 8:
                    lastError += " (version mismatch with Perlin's 1.26.52)";
                    break;
                case 46:
                    lastError += " (the server wants an Xbox login: use --login)";
                    break;
                default: break;
            }
            std::cout << "[bedrock] " << lastError << std::endl;
            state = State::Failed;
        });

        On<ServerToClientHandshakePacket>([this](const ServerToClientHandshakePacket& p) {
            if (!usedRak) {
                // NetherNet (DTLS) already encrypts the link, and the game protocol must not add its own layer on
                // top: answer the handshake in the clear and keep everything else unencrypted.
                std::cout << "[bedrock] ServerToClientHandshake over NetherNet -> replying without encryption"
                          << std::endl;
                Send(ClientToServerHandshakePacket{});
                return;
            }
            auto fresh = std::make_unique<PacketCipher>();
            std::string error;
            if (!fresh->InitFromHandshake(*identity, p.token, error)) {
                lastError = "encryption handshake failed: " + error;
                state = State::Failed;
                return;
            }
            std::cout << "[bedrock] ServerToClientHandshake -> encryption on" << std::endl;
            {
                // Everything after the reply is encrypted, the reply itself included.
                std::lock_guard<std::mutex> lock(txMutex);
                cipher = std::move(fresh);
            }
            Send(ClientToServerHandshakePacket{});
        });

        On<ResourcePacksInfoPacket>([this](const ResourcePacksInfoPacket& info) {
            std::cout << "[bedrock] ResourcePacksInfo (" << info.texturePacks.size() << " packs)" << std::endl;
            state = State::ResourcePacks;
            std::vector<std::string> wanted = packs.Plan(info.texturePacks);
            if (wanted.empty()) {
                SendResourcePackResponse(PackStatus::HaveAllPacks);
            } else {
                packsExpected = wanted.size();
                proto::ResourcePackClientResponsePacket ask;
                ask.responseStatus = PackStatus::SendPacks;
                ask.responseStatusName = "downloading";
                ask.resourcepackids = wanted;
                std::cout << "[bedrock] asking the server for " << wanted.size() << " resource pack(s)" << std::endl;
                Send(ask);
            }
        });

        // Chat and system messages: shown in the log so a waiting room or kick reason is not a mystery.
        OnPartial<TextPacket>([](const TextPacket& t) {
            static int logged = 0;
            if (logged++ < 60)
                std::cout << "[chat] " << (t.sourceName.empty() ? "" : t.sourceName + ": ") << t.message << std::endl;
        });

        On<ResourcePackDataInfoPacket>([this](const ResourcePackDataInfoPacket& info) {
            const auto step = packs.OnDataInfo(info);
            if (step.step == PackDownloader::Step::RequestChunk) {
                Send(proto::ResourcePackChunkRequestPacket{.packId = step.packId, .chunkIndex = step.chunkIndex});
            } else if (step.step == PackDownloader::Step::Failed) {
                lastError = "resource pack download failed: " + step.message;
                state = State::Failed;
            } else if (step.step == PackDownloader::Step::PackDone && packs.AllReceived()) {
                SendResourcePackResponse(PackStatus::HaveAllPacks);
            } else if (step.step == PackDownloader::Step::Ignore && !step.message.empty()) {
                std::cout << "[packs] " << step.message << std::endl;
            }
        });

        On<ResourcePackChunkDataPacket>([this](const ResourcePackChunkDataPacket& chunk) {
            const auto step = packs.OnChunk(chunk);
            if (step.step == PackDownloader::Step::RequestChunk) {
                Send(proto::ResourcePackChunkRequestPacket{.packId = step.packId, .chunkIndex = step.chunkIndex});
            } else if (step.step == PackDownloader::Step::Failed) {
                lastError = "resource pack download failed: " + step.message;
                state = State::Failed;
            } else if (step.step == PackDownloader::Step::PackDone && packs.AllReceived()) {
                SendResourcePackResponse(PackStatus::HaveAllPacks);
            }
        });

        // The server hands us over to another one (a lobby sending you into a game, or a proxy).
        On<TransferPacket>([this](const TransferPacket& t) {
            std::cout << "[bedrock] Transfer -> " << t.serverAddress << ":" << t.port << std::endl;
            transferHost = t.serverAddress;
            transferPort = t.port;
            transferRequested = true;
        });

        On<ResourcePackStackPacket>([this](const ResourcePackStackPacket&) {
            std::cout << "[bedrock] ResourcePackStack" << std::endl;
            SendResourcePackResponse(PackStatus::Completed);
            // Only now: a server that is still waiting for the pack stage to finish treats this as unexpected.
            if (!cacheStatusSent) {
                SendClientCacheStatus();
                cacheStatusSent = true;
            }
            if (!radiusSent) {
                SendRequestChunkRadius();
                radiusSent = true;
            }
        });

        On<ResourcePacksReadyForValidationPacket>([this](const ResourcePacksReadyForValidationPacket&) {
            std::cout << "[bedrock] ResourcePacksReadyForValidation" << std::endl;
            SendResourcePackResponse(PackStatus::Completed);
            if (!cacheStatusSent) {
                SendClientCacheStatus();
                cacheStatusSent = true;
            }
            if (!radiusSent) {
                SendRequestChunkRadius();
                radiusSent = true;
            }
        });

        On<PacketViolationWarningPacket>([](const PacketViolationWarningPacket& p) {
            std::cout << "[bedrock] VIOLATION type=" << static_cast<int>(p.violationType)
                      << " severity=" << static_cast<int>(p.severity) << " packet=" << p.packetId << " ("
                      << PacketName(static_cast<uint32_t>(p.packetId)) << ") reason='"
                      << p.reason.substr(0, 200) << "'" << std::endl;
        });

        OnPartial<StartGamePacket>([this](const StartGamePacket& p) {
            const float x = p.playerPosition.x, y = p.playerPosition.y, z = p.playerPosition.z;
            runtimeId = p.runtimeEntityId;
            uniqueId = p.entityId;
            gameType = static_cast<int>(p.playerGamemode);
            if (gameType == 1) mayFly = true;
            if (!pose.known && SaneWorldPos(x, y, z))
                pose = {x, y, z, p.rotation.z, p.rotation.x, p.rotation.z, false, true};
            if (std::isfinite(x) && std::isfinite(z) && std::fabs(x) <= 100000.0f &&
                std::fabs(z) <= 100000.0f) {
                startPos[0] = x;
                startPos[1] = y;
                startPos[2] = z;
                haveStartPos = true;
            }
            clientGen = p.clientSideGeneration;
            std::cout << "[bedrock] blockNetworkIdsAreHashes=" << (p.blockNetworkIdsAreHashes ? "true" : "false")
                      << std::endl;
            std::cout << "[bedrock] client_side_generation=" << (clientGen ? "true" : "false")
                      << " (requested chunk radius " << chunkRadius << ", max view distance " << maxViewDistance
                      << ")" << std::endl;
            gotSpawn = true;
            serverTick = static_cast<uint64_t>(std::max<int64_t>(p.currentTick, 0));
            authTick = serverTick;
            dimensionId = static_cast<int>(p.dimension);
            rainTarget = p.rainLevel > 0.0f ? 1.0f : 0.0f;
            thunderTarget = p.lightningLevel > 0.0f ? 1.0f : 0.0f;
            std::cout << "[bedrock] StartGame spawn=(" << x << "," << y << "," << z
                      << ") runtime=" << runtimeId << " gamerules=" << p.gamerules.size() << std::endl;
            for (const GameRuleI32& rule : p.gamerules) {
                if (!EqualsIgnoreCase(rule.name, "dodaylightcycle"))
                    continue;
                if (const bool* enabled = std::get_if<bool>(&rule.value)) {
                    SetDayCycle(*enabled);
                    std::cout << "[bedrock] doDaylightCycle=" << *enabled << std::endl;
                }
            }
            std::cout << "[bedrock] time: dayCycleStopTime=" << p.dayCycleStopTime
                      << " currentTick=" << p.currentTick << std::endl;
            double known = 0.0;
            if (!GetWorldTicks(known) && p.currentTick > 0)
                NoteTime(p.currentTick);
            SendRequestChunkRadius();
            SendLoadingScreen(true);
            startGameSeen = true;
            startGameAt = std::chrono::steady_clock::now();
            state = State::InGame;
            std::cout << "[bedrock] StartGame handled -> waiting for PlayerSpawn" << std::endl;
        });

        // The server tells us which area it is streaming. Drop what fell out of it and remember the
        // centre so chunks keep loading around the player as they move.
        On<NetworkChunkPublisherUpdatePacket>([this](const NetworkChunkPublisherUpdatePacket& p) {
            const int cx = static_cast<int>(std::floor(static_cast<double>(p.coordinates.x) / 16.0));
            const int cz = static_cast<int>(std::floor(static_cast<double>(p.coordinates.z) / 16.0));
            const int radius = static_cast<int>((p.radius + 15) / 16);
            viewCenterChunk[0] = cx;
            viewCenterChunk[1] = cz;
            viewRadiusChunks = radius;
            static int logged = 0;
            if (logged++ < 6)
                std::cout << "[chunk] publisher centre (" << p.coordinates.x << "," << p.coordinates.y << ","
                          << p.coordinates.z << ") radius " << p.radius << " blocks, "
                          << p.savedChunks.size() << " server-built chunks listed" << std::endl;
            if (radius > 0)
                PruneChunks(cx, cz, radius + 2);
        });

        On<ChunkRadiusUpdatePacket>([this](const ChunkRadiusUpdatePacket& p) {
            grantedRadius = p.chunkRadius;
            std::cout << "[chunk] server granted chunk radius " << p.chunkRadius << " (asked " << chunkRadius << ")"
                      << std::endl;
        });

        On<ItemRegistryPacket>([](const ItemRegistryPacket& p) {
            for (const ItemstatesEntry& item : p.itemstates)
                if (item.name == "minecraft:shield") {
                    net::context::SetShieldItemID(item.runtimeId);
                    return;
                }
        });

        On<SetTimePacket>([this](const SetTimePacket& p) {
            std::cout << "[bedrock] SetTime " << p.time << std::endl;
            NoteTime(p.time);
        });

        // Since 1.26 the server drives the level time through named world clocks (this is what /time set
        // sends); SetTime is only a legacy fallback. The overworld clock has a fixed hashed id.
        On<SyncWorldClocksPacket>([this](const SyncWorldClocksPacket& p) {
            constexpr uint64_t kOverworldClockId = 0x63d7ede73c38f916ULL;
            if (p.payloadType == SyncWorldClocksPacketPayloadType::InitializeRegistry) {
                for (const WorldClockData& clock : p.clocks)
                    if (clock.id == kOverworldClockId || clock.name == "minecraft:overworld") {
                        overworldClockId = clock.id;
                        std::cout << "[bedrock] clock init " << clock.name << " time=" << clock.time
                                  << " paused=" << clock.paused << std::endl;
                        NoteClock(clock.time, clock.paused);
                    }
            } else if (p.payloadType == SyncWorldClocksPacketPayloadType::SyncState) {
                for (const SyncWorldClockStateData& state : p.syncStates)
                    if (state.clockId == overworldClockId || state.clockId == kOverworldClockId) {
                        std::cout << "[bedrock] clock sync time=" << state.time << " paused=" << state.paused
                                  << std::endl;
                        NoteClock(state.time, state.paused);
                    }
            }
        });

        // Nether / End / back: the old world is gone, chunks of the new dimension follow, and the server waits
        // for DimensionChangeAck (sent from Update once the area around us has arrived).
        On<ChangeDimensionPacket>([this](const ChangeDimensionPacket& p) {
            std::cout << "[bedrock] ChangeDimension " << dimensionId.load() << " -> " << p.dimension << " pos=("
                      << p.position.x << "," << p.position.y << "," << p.position.z << ")" << std::endl;
            dimensionId = p.dimension;
            ResetWorldForDimension();
            if (SaneWorldPos(p.position.x, p.position.y, p.position.z)) {
                pose = {p.position.x, p.position.y - kPlayerEyeHeight, p.position.z, pose.yaw, pose.pitch,
                        pose.headYaw, false, true};
                haveLastAuthPos = false;
                serverMotion.poseChanged = true;
                serverMotion.teleport = true;
                serverMotion.hasVelocity = true;
                serverMotion.velocity[0] = serverMotion.velocity[1] = serverMotion.velocity[2] = 0;
            }
            dimLoadingId = p.loadingScreenId;
            dimAckSince = std::chrono::steady_clock::now();
            dimAckPending = true;
            SendLoadingScreen(true, p.loadingScreenId);
        });

        On<LevelEventPacket>([this](const LevelEventPacket& p) {
            switch (p.event) {
            case LevelEventPacketEvent::StartRain:
                rainTarget = 1.0f;
                break;
            case LevelEventPacketEvent::StopRain:
                rainTarget = 0.0f;
                break;
            case LevelEventPacketEvent::StartThunder:
                thunderTarget = 1.0f;
                break;
            case LevelEventPacketEvent::StopThunder:
                thunderTarget = 0.0f;
                break;
            default:
                break;
            }
        });

        On<GameRulesChangedPacket>([this](const GameRulesChangedPacket& p) {
            for (const GameRuleI32& rule : p.rules)
                if (EqualsIgnoreCase(rule.name, "dodaylightcycle"))
                    if (const bool* enabled = std::get_if<bool>(&rule.value))
                        SetDayCycle(*enabled);
        });

        On<SetSpawnPositionPacket>([this](const SetSpawnPositionPacket& p) {
            const BlockCoordinates& world = p.worldPosition;
            const int32_t worldY = static_cast<int32_t>(world.y);
            std::cout << "[bedrock] SetSpawnPosition type=" << static_cast<int>(p.spawnType) << " pos=("
                      << p.playerPosition.x << "," << static_cast<int32_t>(p.playerPosition.y) << ","
                      << p.playerPosition.z << ") dim=" << p.dimension << " spawn=(" << world.x << ","
                      << worldY << "," << world.z << ")" << std::endl;
            if (worldY < 1000) {
                worldSpawn[0] = world.x;
                worldSpawn[1] = worldY;
                worldSpawn[2] = world.z;
                haveWorldSpawn = true;
            }
        });

        On<UpdateBlockPacket>([this](const UpdateBlockPacket& p) {
            lastBlockPos[0] = p.position.x;
            lastBlockPos[1] = static_cast<int32_t>(p.position.y);
            lastBlockPos[2] = p.position.z;
            haveBlockPos = true;
            if (p.layer == 0)
                netLevel.SetBlock(lastBlockPos[0], lastBlockPos[1], lastBlockPos[2],
                                  BlockPalette::Instance().Intern(static_cast<int32_t>(p.blockRuntimeId)));
            else if (p.layer == 1)
                netLevel.SetLiquid(lastBlockPos[0], lastBlockPos[1], lastBlockPos[2],
                                   BlockPalette::Instance().Intern(static_cast<int32_t>(p.blockRuntimeId)));
        });

        On<UpdateBlockSyncedPacket>([this](const UpdateBlockSyncedPacket& p) {
            const int x = p.position.x, y = static_cast<int32_t>(p.position.y), z = p.position.z;
            const uint16_t id = BlockPalette::Instance().Intern(static_cast<int32_t>(p.blockRuntimeId));
            if (p.layer == 0)
                netLevel.SetBlock(x, y, z, id);
            else if (p.layer == 1)
                netLevel.SetLiquid(x, y, z, id);
        });

        On<MovePlayerPacket>([this](const MovePlayerPacket& p) {
            if (runtimeId != 0 && p.runtimeId != runtimeId)
                return;
            runtimeId = p.runtimeId;
            const Vec3F& eye = p.position;
            if (!SaneWorldPos(eye.x, eye.y, eye.z))
                return;
            pose = {eye.x, eye.y - kPlayerEyeHeight, eye.z, p.yaw, p.pitch, p.headYaw, p.onGround, true};
            haveLastAuthPos = false;
            gotSpawn = true;
            const bool forced = p.mode == MovePlayerPacketMode::Teleport || p.mode == MovePlayerPacketMode::Reset;
            if (forced) {
                pendingTeleportAck = true;
                serverMotion.poseChanged = true;
                serverMotion.teleport = true;
                serverMotion.hasRotation = true;
                serverMotion.yaw = p.yaw;
                serverMotion.pitch = p.pitch;
                serverMotion.hasVelocity = true;
                serverMotion.velocity[0] = serverMotion.velocity[1] = serverMotion.velocity[2] = 0;
            }
        });

        On<RespawnPacket>([this](const RespawnPacket& p) {
            std::cout << "[bedrock] Respawn pos=(" << p.position.x << "," << p.position.y << ","
                      << p.position.z << ") state=" << static_cast<int>(p.state) << std::endl;
            if (p.state != 1)
                return;
            if (SaneWorldPos(p.position.x, p.position.y, p.position.z)) {
                pose = {p.position.x, p.position.y - kPlayerEyeHeight,
                        p.position.z, pose.yaw,
                        pose.pitch,   pose.headYaw,
                        false,        true};
                gotSpawn = true;
                haveLastAuthPos = false;
                serverMotion.poseChanged = true;
                serverMotion.teleport = true;
                serverMotion.hasVelocity = true;
                serverMotion.velocity[0] = serverMotion.velocity[1] = serverMotion.velocity[2] = 0;
            }
            RespawnPacket reply;
            reply.position = p.position;
            reply.state = 2;
            reply.runtimeEntityId = runtimeId;
            Send(reply);
        });

        // The server disagreed with our prediction: adopt its position and velocity.
        On<CorrectPlayerMovePredictionPacket>([this](const CorrectPlayerMovePredictionPacket& p) {
            if (p.predictionType != CorrectPlayerMovePredictionPacketPredictionType::Player)
                return;
            if (!SaneWorldPos(p.position.x, p.position.y, p.position.z))
                return;
            pose.x = p.position.x;
            pose.y = p.position.y - kPlayerEyeHeight;
            pose.z = p.position.z;
            pose.onGround = p.onGround;
            haveLastAuthPos = false;
            if (correctionsReceived++ < 5 || correctionsReceived % 100 == 0)
                std::cout << "[move] server correction #" << correctionsReceived << " tick=" << p.tick << " pos=("
                          << p.position.x << "," << p.position.y << "," << p.position.z << ") delta=("
                          << p.delta.x << "," << p.delta.y << "," << p.delta.z << ")" << std::endl;
            serverMotion.poseChanged = true;
            serverMotion.correction = true;
            serverMotion.tick = p.tick;
            serverMotion.onGround = p.onGround;
            serverMotion.pos[0] = pose.x;
            serverMotion.pos[1] = pose.y;
            serverMotion.pos[2] = pose.z;
            serverMotion.hasVelocity = true;
            serverMotion.velocity[0] = p.delta.x;
            serverMotion.velocity[1] = p.delta.y;
            serverMotion.velocity[2] = p.delta.z;
        });

        On<SetEntityMotionPacket>([this](const SetEntityMotionPacket& p) {
            if (runtimeId == 0 || p.runtimeEntityId != runtimeId)
                return;
            serverMotion.hasVelocity = true;
            serverMotion.velocity[0] = p.velocity.x;
            serverMotion.velocity[1] = p.velocity.y;
            serverMotion.velocity[2] = p.velocity.z;
            NoteKnockback(p.velocity.x, p.velocity.z);
        });

        On<UpdateAttributesPacket>([this](const UpdateAttributesPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId != runtimeId)
                return;
            for (const PlayerAttributesEntry& a : p.attributes) {
                if (a.name == "minecraft:movement" && a.current > 0.0f && a.current < 5.0f)
                    movementSpeed = a.current;
                if (a.name == "minecraft:health") {
                    // A drop without a hurt event (the server sends one for most damage) still counts.
                    const bool recent = std::chrono::steady_clock::now() - lastHurtAt < std::chrono::milliseconds(250);
                    if (lastHealth >= 0.0f && a.current < lastHealth - 0.01f && a.current > 0.0f && !recent)
                        NoteHurt();
                    lastHealth = a.current;
                }
            }
        });

        On<EntityEventPacket>([this](const EntityEventPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId && p.eventId == EntityEventPacketEventId::HurtAnimation)
                NoteHurt();
        });

        On<UpdateAbilitiesPacket>([this](const UpdateAbilitiesPacket& p) {
            if (runtimeId != 0 && p.entityUniqueId != 0 && uniqueId != 0 && p.entityUniqueId != uniqueId)
                return;
            // Later layers override earlier ones for every ability they are allowed to set.
            bool fly = false, flying = false, clip = false;
            for (const AbilityLayers& layer : p.abilities) {
                if (layer.allowed & (1u << 10)) fly = (layer.enabled & (1u << 10)) != 0;
                if (layer.allowed & (1u << 9)) flying = (layer.enabled & (1u << 9)) != 0;
                if (layer.allowed & (1u << 17)) clip = (layer.enabled & (1u << 17)) != 0;
                if (layer.flySpeed > 0) flySpeed = layer.flySpeed;
                if (layer.verticalFlySpeed > 0) vFlySpeed = layer.verticalFlySpeed;
                if (layer.walkSpeed > 0) walkSpeed = layer.walkSpeed;
            }
            mayFly = fly;
            isFlying = flying;
            noClip = clip;
            std::cout << "[bedrock] abilities mayFly=" << mayFly << " flying=" << isFlying
                      << " noclip=" << noClip << " fly=" << flySpeed << std::endl;
        });

        On<UpdatePlayerGameTypePacket>([this](const UpdatePlayerGameTypePacket& p) {
            if (uniqueId != 0 && p.playerUniqueId != uniqueId)
                return;
            gameType = static_cast<int>(p.gamemode);
        });
        On<SetPlayerGameTypePacket>([this](const SetPlayerGameTypePacket& p) {
            gameType = static_cast<int>(p.gamemode);
        });

        On<NetworkStackLatencyPacket>([this](const NetworkStackLatencyPacket& p) {
            if (p.needsResponse)
                SendTickSync(p.timestamp);
        });

        On<CommandOutputPacket>([](const CommandOutputPacket& p) {
            std::cout << "[bedrock] CommandOutput success=" << p.successCount << " msgs=" << p.output.size();
            for (size_t i = 0; i < p.output.size() && i < 4; i++) {
                std::cout << " [" << p.output[i].messageId;
                for (size_t j = 0; j < p.output[i].parameters.size() && j < 4; j++)
                    std::cout << " '" << p.output[i].parameters[j].substr(0, 40) << "'";
                std::cout << "]";
            }
            std::cout << std::endl;
        });

        if (std::getenv("PERLIN_TRACE")) {
            Trace<StartGamePacket>(*this);
            Trace<UpdateAbilitiesPacket>(*this);
            Trace<TextPacket>(*this);
            Trace<CommandOutputPacket>(*this);
            Trace<MovePlayerPacket>(*this);
        }

        On<LevelChunkPacket>([this](const LevelChunkPacket& p) { HandleLevelChunk(p); });
        On<SubChunkPacket>([this](const SubChunkPacket& p) { HandleSubChunk(p); });
    }

}
