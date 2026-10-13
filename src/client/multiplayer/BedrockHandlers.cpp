#include "client/ui/ChatFormat.hpp"
#include "client/multiplayer/BedrockSession.hpp"
#include "network/ClientIdentity.hpp"
#include "network/PacketDump.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <atomic>
#include <cctype>
#include <variant>
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

        std::string UuidKey(const Uuid& u) {
            static const char* hex = "0123456789abcdef";
            std::string s;
            for (uint8_t b : u.bytes) {
                s += hex[b >> 4];
                s += hex[b & 15];
            }
            return s;
        }

        bool MetaNumber(const MetadataDictionaryEntry& m, double& out) {
            if (const auto* v = std::get_if<1>(&m.value)) { out = static_cast<double>(*v); return true; }
            if (const auto* v = std::get_if<2>(&m.value)) { out = static_cast<double>(*v); return true; }
            if (const auto* v = std::get_if<3>(&m.value)) { out = static_cast<double>(*v); return true; }
            if (const auto* inner = std::get_if<4>(&m.value)) {
                if (const auto* v = std::get_if<1>(inner)) { out = *v; return true; }
                if (const auto* v = std::get_if<2>(inner)) { out = *v; return true; }
                if (const auto* v = std::get_if<3>(inner)) { out = *v; return true; }
                if (const auto* v = std::get_if<4>(inner)) { out = *v; return true; }
                if (const auto* v = std::get_if<8>(inner)) { out = static_cast<double>(*v); return true; }
            }
            return false;
        }

        void ApplyMetadata(entity::EntityTracker& tracker, uint64_t rid, const std::vector<MetadataDictionaryEntry>& metadata) {
            int variant = -1, mark = -1, color = -1;
            for (const MetadataDictionaryEntry& m : metadata) {
                double n = 0;
                if (!MetaNumber(m, n))
                    continue;
                switch (m.key) {
                case MetadataDictionaryEntryKey::Flags: tracker.SetFlags(rid, static_cast<uint64_t>(static_cast<int64_t>(n))); break;
                case MetadataDictionaryEntryKey::Scale: tracker.SetScale(rid, static_cast<float>(n)); break;
                case MetadataDictionaryEntryKey::Variant: variant = static_cast<int>(n); break;
                case MetadataDictionaryEntryKey::MarkVariant: mark = static_cast<int>(n); break;
                case MetadataDictionaryEntryKey::Color: color = static_cast<int>(n); break;
                default: break;
                }
            }
            if (variant >= 0 || mark >= 0 || color >= 0)
                tracker.SetVariant(rid, std::max(variant, 0), std::max(mark, 0), std::max(color, 0));
        }

        uint64_t MetaFlags(const std::vector<MetadataDictionaryEntry>& metadata) {
            for (const MetadataDictionaryEntry& m : metadata) {
                double n = 0;
                if (m.key == MetadataDictionaryEntryKey::Flags && MetaNumber(m, n))
                    return static_cast<uint64_t>(static_cast<int64_t>(n));
            }
            return 0;
        }

        std::shared_ptr<const entity::SkinData> ConvertSkin(const Skin& s) {
            static std::atomic<uint64_t> counter{1};
            auto out = std::make_shared<entity::SkinData>();
            out->serial = counter++;
            out->skinId = s.skinId;
            out->skinWidth = s.skinData.width;
            out->skinHeight = s.skinData.height;
            out->skinRgba = s.skinData.data;
            if (static_cast<size_t>(out->skinWidth) * static_cast<size_t>(out->skinHeight) * 4 != out->skinRgba.size()) {
                out->skinWidth = out->skinHeight = 0;
                out->skinRgba.clear();
            }
            out->capeWidth = s.capeData.width;
            out->capeHeight = s.capeData.height;
            out->capeRgba = s.capeData.data;
            if (static_cast<size_t>(out->capeWidth) * static_cast<size_t>(out->capeHeight) * 4 != out->capeRgba.size()) {
                out->capeWidth = out->capeHeight = 0;
                out->capeRgba.clear();
            }
            out->slim = s.armSize == SkinArmSize::Slim;
            if (!s.geometryData.empty() && s.geometryData.find("geometry") != std::string::npos) {
                out->geometryJson = s.geometryData;

                const std::string& patch = s.skinResourcePack;
                const size_t at = patch.find("\"default\"");
                if (at != std::string::npos) {
                    const size_t q1 = patch.find('"', patch.find(':', at) + 1);
                    const size_t q2 = q1 == std::string::npos ? q1 : patch.find('"', q1 + 1);
                    if (q2 != std::string::npos)
                        out->geometryName = patch.substr(q1 + 1, q2 - q1 - 1);
                }
                if (out->geometryName.empty())
                    out->geometryName = "geometry.humanoid.custom";
            }
            return out;
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

        OnPartial<TextPacket>([this](const TextPacket& t) {
            const int type = static_cast<int>(t.type);
            if (type == 3 || type == 4 || type == 5)
                return;
            const std::string line = ui::FormatTextPacket(type, t.needsTranslation, t.sourceName, t.message, t.parameters);
            if (line.empty())
                return;
            std::cout << "[chat] " << line << std::endl;
            PushChat(line);
        });
        OnPartial<CommandOutputPacket>([this](const CommandOutputPacket& c) {
            for (const auto& e : c.output) {
                const std::string line = ui::FormatCommandOutput(e.messageId, e.success, e.parameters);
                if (!line.empty())
                    PushChat(line);
            }
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

        On<TransferPacket>([this](const TransferPacket& t) {
            std::cout << "[bedrock] Transfer -> " << t.serverAddress << ":" << t.port << std::endl;
            transferHost = t.serverAddress;
            transferPort = t.port;
            transferRequested = true;
        });

        On<ResourcePackStackPacket>([this](const ResourcePackStackPacket&) {
            std::cout << "[bedrock] ResourcePackStack" << std::endl;
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
            serverAuthBreaking = p.serverAuthoritativeBlockBreaking;
            std::cout << "[bedrock] serverAuthoritativeBlockBreaking=" << (p.serverAuthoritativeBlockBreaking ? "true" : "false") << std::endl;
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

        On<ItemRegistryPacket>([this](const ItemRegistryPacket& p) {
            {
                std::lock_guard lock(statusMutex);
                itemNames.clear();
                for (const ItemstatesEntry& item : p.itemstates)
                    itemNames[item.runtimeId] = item.name;
            }
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

        On<ChangeDimensionPacket>([this](const ChangeDimensionPacket& p) {
            entities.Clear();
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

        On<PlaySoundPacket>([this](const PlaySoundPacket& p) {
            SoundRequest r;
            r.kind = SoundRequest::Kind::Named;
            r.name = p.name;
            r.x = static_cast<float>(p.coordinates.x) / 8.0f;
            r.y = static_cast<float>(p.coordinates.y) / 8.0f;
            r.z = static_cast<float>(p.coordinates.z) / 8.0f;
            r.volume = p.volume;
            r.pitch = p.pitch;
            r.global = p.bypassListenerRangeCheck;
            PushSound(std::move(r));
        });

        On<StopSoundPacket>([this](const StopSoundPacket& p) {
            SoundRequest r;
            r.kind = SoundRequest::Kind::Stop;
            r.name = p.name;
            r.stopAll = p.stopAll;
            PushSound(std::move(r));
        });

        On<LevelSoundEventPacket>([this](const LevelSoundEventPacket& p) {
            SoundRequest r;
            r.kind = SoundRequest::Kind::Level;
            r.name = p.soundId;
            r.x = p.position.x;
            r.y = p.position.y;
            r.z = p.position.z;
            r.extra = p.extraData;
            r.entityType = p.entityType;
            r.baby = p.isBabyMob;
            r.global = p.isGlobal;
            PushSound(std::move(r));
        });

        On<LevelEventPacket>([this](const LevelEventPacket& p) {

            if (p.event == LevelEventPacketEvent::BlockStartBreak || p.event == LevelEventPacketEvent::BlockStopBreak ||
                p.event == LevelEventPacketEvent::BlockBreakSpeed) {
                const int bx = static_cast<int>(std::floor(p.position.x)), by = static_cast<int>(std::floor(p.position.y)),
                          bz = static_cast<int>(std::floor(p.position.z));
                std::lock_guard lock(crackMutex);
                const auto now = std::chrono::steady_clock::now();
                const auto it = std::find_if(crackStates.begin(), crackStates.end(),
                                             [&](const CrackState& c) { return c.x == bx && c.y == by && c.z == bz; });
                if (p.event == LevelEventPacketEvent::BlockStopBreak) {
                    if (it != crackStates.end())
                        crackStates.erase(it);
                } else {
                    CrackState* c = it != crackStates.end() ? &*it : &crackStates.emplace_back();
                    if (it == crackStates.end() || c->since == std::chrono::steady_clock::time_point{}) {
                        c->x = bx; c->y = by; c->z = bz; c->progress = 0.0;
                    } else {
                        c->progress = std::min(1.0, c->progress + c->perSecond * std::chrono::duration<double>(now - c->since).count());
                    }
                    c->since = now;
                    c->perSecond = static_cast<double>(p.data) / 65535.0 * 20.0;
                }
                return;
            }

            const auto named = [&](const char* sound, float volume = 1.0f, float pitch = 1.0f) {
                SoundRequest r;
                r.name = sound;
                r.x = p.position.x;
                r.y = p.position.y;
                r.z = p.position.z;
                r.volume = volume;
                r.pitch = pitch;
                PushSound(std::move(r));
            };
            switch (p.event) {
            case LevelEventPacketEvent::SoundClick: named("random.click", 1.0f, 1.0f); break;
            case LevelEventPacketEvent::SoundClickFail: named("random.click", 1.0f, 0.8f); break;
            case LevelEventPacketEvent::SoundShoot: named("random.bow"); break;
            case LevelEventPacketEvent::SoundDoor: named("random.door_open"); break;
            case LevelEventPacketEvent::SoundFizz: named("random.fizz", 0.5f); break;
            case LevelEventPacketEvent::SoundIgnite: named("fire.ignite"); break;
            case LevelEventPacketEvent::SoundGhast: named("mob.ghast.moan"); break;
            case LevelEventPacketEvent::SoundGhastShoot: named("mob.ghast.fireball"); break;
            case LevelEventPacketEvent::SoundBlazeShoot: named("mob.blaze.shoot"); break;
            case LevelEventPacketEvent::SoundEndermanTeleport: named("mob.endermen.portal"); break;
            case LevelEventPacketEvent::SoundAnvilBreak: named("random.anvil_break"); break;
            case LevelEventPacketEvent::SoundAnvilUse: named("random.anvil_use"); break;
            case LevelEventPacketEvent::SoundAnvilFall: named("random.anvil_land"); break;
            case LevelEventPacketEvent::SoundPop: named("random.pop"); break;
            case LevelEventPacketEvent::SoundPortal: named("portal.portal"); break;
            case LevelEventPacketEvent::SoundOrb: named("random.orb"); break;
            case LevelEventPacketEvent::SoundTotem: named("random.totem"); break;
            case LevelEventPacketEvent::SoundCamera: named("camera.take_picture"); break;
            case LevelEventPacketEvent::ParticleDestroy: {
                SoundRequest r;
                r.kind = SoundRequest::Kind::Level;
                r.name = "break";
                r.x = p.position.x;
                r.y = p.position.y;
                r.z = p.position.z;
                r.extra = p.data;
                PushSound(std::move(r));
                break;
            }
            default:
                break;
            }
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
            static int blockLogs = 0;
            if (blockLogs < 400 && (std::getenv("PERLIN_BLOCK_DEBUG") || blockLogs++ < 400))
                std::cout << "[block] update_block (" << p.position.x << "," << p.position.y << "," << p.position.z << ") layer " << p.layer
                          << " id " << static_cast<int32_t>(p.blockRuntimeId) << " "
                          << BlockPalette::Instance().Name(BlockPalette::Instance().Intern(static_cast<int32_t>(p.blockRuntimeId))) << std::endl;
            NoteUnknownBlock(static_cast<int32_t>(p.blockRuntimeId));
            if (p.layer == 0)
                ConfirmPlace(p.position.x, static_cast<int32_t>(p.position.y), p.position.z, static_cast<int32_t>(p.blockRuntimeId));
            if (p.layer == 0)
                netLevel.SetBlock(lastBlockPos[0], lastBlockPos[1], lastBlockPos[2],
                                  BlockPalette::Instance().Intern(static_cast<int32_t>(p.blockRuntimeId)));
            else if (p.layer == 1)
                netLevel.SetLiquid(lastBlockPos[0], lastBlockPos[1], lastBlockPos[2],
                                   BlockPalette::Instance().Intern(static_cast<int32_t>(p.blockRuntimeId)));
        });

        On<UpdateSubchunkBlocksPacket>([this](const UpdateSubchunkBlocksPacket& p) {

            const auto sane = [&](const BlockUpdate& b) {
                const int32_t y = static_cast<int32_t>(b.position.y);
                if (y < -64 || y >= 320 || std::abs(b.position.x) > 30000000 || std::abs(b.position.z) > 30000000)
                    return false;
                return netLevel.HasSectionAt(b.position.x, y, b.position.z);
            };
            static int logged = 0;
            if (logged < 100) {
                logged++;
                int ok = 0;
                for (const BlockUpdate& b : p.blocks) ok += sane(b) ? 1 : 0;
                std::cout << "[block] update_subchunk_blocks at (" << p.x << "," << p.y << "," << p.z << ") " << p.blocks.size()
                          << " blocks (" << ok << " applied), " << p.extra.size() << " extra";
                if (!p.blocks.empty())
                    std::cout << "; first (" << p.blocks[0].position.x << "," << static_cast<int32_t>(p.blocks[0].position.y) << ","
                              << p.blocks[0].position.z << ") rid " << p.blocks[0].runtimeId;
                std::cout << std::endl;
            }
            for (const BlockUpdate& b : p.blocks) {
                if (!sane(b))
                    continue;
                NoteUnknownBlock(static_cast<int32_t>(b.runtimeId));
                ConfirmPlace(b.position.x, static_cast<int32_t>(b.position.y), b.position.z, static_cast<int32_t>(b.runtimeId));
                netLevel.SetBlock(b.position.x, static_cast<int32_t>(b.position.y), b.position.z,
                                  BlockPalette::Instance().Intern(static_cast<int32_t>(b.runtimeId)));
            }
            for (const BlockUpdate& b : p.extra)
                if (sane(b))
                    netLevel.SetLiquid(b.position.x, static_cast<int32_t>(b.position.y), b.position.z,
                                       BlockPalette::Instance().Intern(static_cast<int32_t>(b.runtimeId)));
        });

        On<UpdateBlockSyncedPacket>([this](const UpdateBlockSyncedPacket& p) {
            const int x = p.position.x, y = static_cast<int32_t>(p.position.y), z = p.position.z;
            const uint16_t id = BlockPalette::Instance().Intern(static_cast<int32_t>(p.blockRuntimeId));
            if (p.layer == 0)
                ConfirmPlace(x, y, z, static_cast<int32_t>(p.blockRuntimeId));
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

        On<DeathInfoPacket>([this](const DeathInfoPacket& p) {
            const std::string text = ui::FormatTextPacket(2, true, std::string(), p.cause, p.messages);
            std::cout << "[bedrock] death_info " << p.cause << " -> " << text << std::endl;
            std::lock_guard<std::mutex> lk(deathMutex);
            deathCause = text;
            deathPending = true;
        });

        On<RespawnPacket>([this](const RespawnPacket& p) {
            std::cout << "[bedrock] Respawn pos=(" << p.position.x << "," << p.position.y << ","
                      << p.position.z << ") state=" << static_cast<int>(p.state) << std::endl;
            if (p.state != 1)
                return;
            {
                std::lock_guard<std::mutex> lk(deathMutex);
                deathCause.clear();
                deathPending = false;
            }
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

            proto::PlayerActionPacket done;
            done.runtimeEntityId = runtimeId;
            done.action = static_cast<proto::Action>(7);
            done.position = {0, 0, 0};
            done.resultPosition = {0, 0, 0};
            done.face = -1;
            std::cout << "[bedrock] respawn ready -> PlayerAction Respawn" << std::endl;
            Send(done);
        });

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

        On<AddPlayerPacket>([this](const AddPlayerPacket& p) {
            if (runtimeId != 0 && p.runtimeId == runtimeId)
                return;
            std::string held;
            if (const auto it = itemNames.find(p.heldItem.networkId); p.heldItem.networkId != 0 && it != itemNames.end())
                held = it->second;
            entities.AddPlayer(p.runtimeId, p.uniqueId, UuidKey(p.uuid), p.username,
                               {p.position.x, p.position.y, p.position.z}, p.pitch, p.yaw, p.headYaw, MetaFlags(p.metadata), held);
            ApplyMetadata(entities, p.runtimeId, p.metadata);
        });

        On<AddEntityPacket>([this](const AddEntityPacket& p) {
            if (runtimeId != 0 && p.runtimeId == runtimeId)
                return;
            entities.AddEntity(p.runtimeId, p.uniqueId, p.entityType, {p.position.x, p.position.y, p.position.z}, p.pitch, p.yaw,
                               p.headYaw, MetaFlags(p.metadata), 1.0f, 0, 0, 0);
            ApplyMetadata(entities, p.runtimeId, p.metadata);
            for (const EntityAttributesEntry& a : p.attributes)
                if (a.name == "minecraft:health")
                    entities.SetHealth(p.runtimeId, a.value);
        });

        On<AddItemEntityPacket>([this](const AddItemEntityPacket& p) {
            const auto it = itemNames.find(p.item.networkId);
            if (p.item.networkId == 0 || it == itemNames.end() || p.item.count == 0)
                return;
            entities.AddItem(p.runtimeEntityId, p.entityIdSelf, {p.position.x, p.position.y, p.position.z}, it->second, p.item.count,
                             static_cast<int>(p.item.metadata), p.item.blockRuntimeId, false);
        });

        On<TakeItemEntityPacket>([this](const TakeItemEntityPacket& p) {

            entity::Entity item;
            if (runtimeId != 0 && p.target == runtimeId && entities.Find(p.runtimeEntityId, item) && item.isItem && item.itemCount > 0) {
                std::lock_guard lock(statusMutex);
                int left = item.itemCount;
                const auto fits = [&](const ItemStackInfo& s) { return s.name == item.itemName && s.aux == item.itemAux && s.count < 64; };
                for (ItemStackInfo* s : {&inventory.hotbar[0], &inventory.hotbar[1], &inventory.hotbar[2], &inventory.hotbar[3],
                                         &inventory.hotbar[4], &inventory.hotbar[5], &inventory.hotbar[6], &inventory.hotbar[7],
                                         &inventory.hotbar[8]})
                    if (left > 0 && !s->Empty() && fits(*s)) {
                        const int add = std::min(left, 64 - s->count);
                        s->count += add;
                        left -= add;
                    }
                for (ItemStackInfo& s : inventory.hotbar)
                    if (left > 0 && s.Empty()) {
                        s.name = item.itemName;
                        s.aux = item.itemAux;
                        s.blockRuntimeId = item.itemBlock;
                        s.enchanted = item.itemEnchanted;
                        s.damage = 0;
                        s.count = std::min(left, 64);
                        left -= s.count;
                    }
            }
            entities.RemoveRuntime(p.runtimeEntityId);
        });

        On<RemoveEntityPacket>([this](const RemoveEntityPacket& p) { entities.Remove(p.entityIdSelf); });

        On<MoveEntityPacket>([this](const MoveEntityPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId)
                return;
            constexpr float kByte = 360.0f / 256.0f;
            entities.Move(p.runtimeEntityId, {p.position.x, p.position.y, p.position.z}, p.rotation.pitch * kByte,
                          p.rotation.yaw * kByte, p.rotation.headYaw * kByte, (p.flags & 1) != 0);
        });

        On<MoveEntityDeltaPacket>([this](const MoveEntityDeltaPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId)
                return;
            constexpr float kByte = 360.0f / 256.0f;
            double x = 0, y = 0, z = 0;
            float pitch = 0, yaw = 0, head = 0;
            if (p.x) x = *p.x;
            if (p.y) y = *p.y;
            if (p.z) z = *p.z;
            if (p.rotX) pitch = *p.rotX * kByte;
            if (p.rotY) yaw = *p.rotY * kByte;
            if (p.rotZ) head = *p.rotZ * kByte;
            entities.MoveDelta(p.runtimeEntityId, p.x ? &x : nullptr, p.y ? &y : nullptr, p.z ? &z : nullptr,
                               p.rotX ? &pitch : nullptr, p.rotY ? &yaw : nullptr, p.rotZ ? &head : nullptr, p.onGround);
        });

        On<MovePlayerPacket>([this](const MovePlayerPacket& p) {
            if (runtimeId != 0 && p.runtimeId == runtimeId)
                return;
            entities.Move(p.runtimeId, {p.position.x, p.position.y, p.position.z}, p.pitch, p.yaw, p.headYaw, p.onGround);
        });

        On<SetEntityDataPacket>([this](const SetEntityDataPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId)
                return;
            ApplyMetadata(entities, p.runtimeEntityId, p.metadata);
        });

        On<PlayerListPacket>([this](const PlayerListPacket& p) {
            for (const PlayerRecord& r : p.records) {
                const std::string key = UuidKey(r.uuid);
                if (r.type == PlayerRecordType::Add) {
                    auto skin = ConvertSkin(r.skinData);
                    entities.SetSkin(key, skin);
                    std::lock_guard<std::mutex> lk(tabMutex);
                    bool found = false;
                    for (TabPlayer& t : tabPlayers)
                        if (t.uuid == key) {
                            t.name = r.username;
                            t.skin = skin;
                            found = true;
                        }
                    if (!found)
                        tabPlayers.push_back({key, r.username, skin});
                } else {
                    std::lock_guard<std::mutex> lk(tabMutex);
                    tabPlayers.erase(std::remove_if(tabPlayers.begin(), tabPlayers.end(), [&](const TabPlayer& t) { return t.uuid == key; }),
                                     tabPlayers.end());
                }
            }
        });

        On<PlayerSkinPacket>([this](const PlayerSkinPacket& p) { entities.SetSkin(UuidKey(p.uuid), ConvertSkin(p.skin)); });

        On<MobEquipmentPacket>([this](const MobEquipmentPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId)
                return;
            std::string name;
            if (const auto it = itemNames.find(p.item.networkId); p.item.networkId != 0 && it != itemNames.end())
                name = it->second;
            entities.SetHeld(p.runtimeEntityId, name);
        });

        On<AnimatePacket>([this](const AnimatePacket& p) {
            if (p.actionId == AnimatePacketActionId::SwingArm && !(runtimeId != 0 && p.runtimeEntityId == runtimeId))
                entities.Swing(p.runtimeEntityId);
        });

        On<EntityEventPacket>([this](const EntityEventPacket& p) {
            if (p.eventId == EntityEventPacketEventId::HurtAnimation && !(runtimeId != 0 && p.runtimeEntityId == runtimeId))
                entities.Hurt(p.runtimeEntityId);
            if (p.eventId == EntityEventPacketEventId::DeathAnimation && !(runtimeId != 0 && p.runtimeEntityId == runtimeId))
                entities.Die(p.runtimeEntityId);
        });

        On<UpdateAttributesPacket>([this](const UpdateAttributesPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId != runtimeId) {
                for (const PlayerAttributesEntry& a : p.attributes)
                    if (a.name == "minecraft:health")
                        entities.SetHealth(p.runtimeEntityId, a.current);
                return;
            }
            for (const PlayerAttributesEntry& a : p.attributes) {
                if (a.name == "minecraft:movement" && a.current > 0.0f && a.current < 5.0f)
                    movementSpeed = a.current;
                if (a.name == "minecraft:health") {

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
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId && p.eventId == EntityEventPacketEventId::DeathAnimation) {
                std::cout << "[bedrock] own death animation" << std::endl;
                std::lock_guard<std::mutex> lk(deathMutex);
                deathPending = true;
            }
        });

        On<EntityEventPacket>([this](const EntityEventPacket& p) {
            const char* event = nullptr;
            const char* direct = nullptr;
            switch (p.eventId) {
            case EntityEventPacketEventId::HurtAnimation: event = "hurt"; break;
            case EntityEventPacketEventId::DeathAnimation: event = "death"; break;
            case EntityEventPacketEventId::ElderGuardianCurse: event = "elderguardian.curse"; break;
            case EntityEventPacketEventId::EatingItem: event = "eat"; break;
            case EntityEventPacketEventId::WitchDrinkPotion: event = "drink"; break;
            case EntityEventPacketEventId::PlayerAddXpLevels: direct = "random.levelup"; break;
            case EntityEventPacketEventId::CreeperPrimeFuse: direct = "random.fuse"; break;
            default: break;
            }
            if (!event && !direct)
                return;
            SoundRequest r;
            const bool self = runtimeId != 0 && p.runtimeEntityId == runtimeId;
            if (self && (p.eventId == EntityEventPacketEventId::EatingItem))
                return;
            if (self) {
                r.entityType = "minecraft:player";
                r.x = pose.x;
                r.y = pose.y;
                r.z = pose.z;
            } else {
                entity::Entity e;
                if (!entities.Find(p.runtimeEntityId, e))
                    return;
                r.entityType = e.type;
                r.x = static_cast<float>(e.pos.x);
                r.y = static_cast<float>(e.pos.y);
                r.z = static_cast<float>(e.pos.z);
                r.baby = e.Has(entity::flag::Baby);
            }
            if (direct) {
                r.kind = SoundRequest::Kind::Named;
                r.name = direct;
                r.entityType.clear();
            } else {
                r.kind = SoundRequest::Kind::Level;
                r.name = event;
            }
            PushSound(std::move(r));
        });

        On<UpdateAbilitiesPacket>([this](const UpdateAbilitiesPacket& p) {
            if (runtimeId != 0 && p.entityUniqueId != 0 && uniqueId != 0 && p.entityUniqueId != uniqueId)
                return;

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
        On<SubchunkPacket>([this](const SubchunkPacket& p) { HandleSubChunk(p); });
        RegisterPlayerStateHandlers();
    }

}
