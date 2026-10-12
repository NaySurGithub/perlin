#include "client/multiplayer/BedrockSession.hpp"
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
                    lastError = "login rejected, PlayStatus=" + std::to_string(static_cast<int>(p.status));
                    state = State::Failed;
                }
            } else if (p.status == PlayStatusPacketStatus::PlayerSpawn) {
                SendSetLocalPlayerAsInitialized();
                state = State::InGame;
                std::cout << "[bedrock] spawned -> InGame" << std::endl;
            }
        });

        On<DisconnectPacket>([this](const DisconnectPacket& p) {
            lastError = "server disconnect, reason=" + std::to_string(static_cast<int>(p.reason)) +
                        (p.message.empty() ? "" : " message='" + p.message + "'");
            std::cout << "[bedrock] " << lastError << std::endl;
            state = State::Failed;
        });

        On<ServerToClientHandshakePacket>([this](const ServerToClientHandshakePacket&) {
            std::cout << "[bedrock] ServerToClientHandshake -> replying (no encryption)" << std::endl;
            Send(ClientToServerHandshakePacket{});
        });

        On<ResourcePacksInfoPacket>([this](const ResourcePacksInfoPacket&) {
            std::cout << "[bedrock] ResourcePacksInfo" << std::endl;
            SendResourcePackResponse(PackStatus::Completed);
            state = State::ResourcePacks;
            if (!cacheStatusSent) {
                SendClientCacheStatus();
                cacheStatusSent = true;
            }
            if (!radiusSent) {
                SendRequestChunkRadius();
                radiusSent = true;
            }
        });

        On<ResourcePackStackPacket>([this](const ResourcePackStackPacket&) {
            std::cout << "[bedrock] ResourcePackStack" << std::endl;
            SendResourcePackResponse(PackStatus::Completed);
        });

        On<ResourcePacksReadyForValidationPacket>([this](const ResourcePacksReadyForValidationPacket&) {
            std::cout << "[bedrock] ResourcePacksReadyForValidation" << std::endl;
            SendResourcePackResponse(PackStatus::Completed);
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
            if (!pose.known && SaneWorldPos(x, y, z))
                pose = {x, y, z, p.rotation.z, p.rotation.x, p.rotation.z, false, true};
            if (std::isfinite(x) && std::isfinite(z) && std::fabs(x) <= 100000.0f &&
                std::fabs(z) <= 100000.0f) {
                startPos[0] = x;
                startPos[1] = y;
                startPos[2] = z;
                haveStartPos = true;
            }
            gotSpawn = true;
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
            SendRequestChunkRadius();
            SendSetLocalPlayerAsInitialized();
            state = State::InGame;
            std::cout << "[bedrock] spawned -> InGame" << std::endl;
        });

        On<ItemRegistryPacket>([](const ItemRegistryPacket& p) {
            for (const ItemstatesEntry& item : p.itemstates)
                if (item.name == "minecraft:shield") {
                    net::context::SetShieldItemID(item.runtimeId);
                    return;
                }
        });

        On<SetTimePacket>([this](const SetTimePacket& p) {
            static int logged = 0;
            if (logged++ < 5)
                std::cout << "[bedrock] SetTime " << p.time << std::endl;
            NoteTime(p.time);
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
            if (p.mode == MovePlayerPacketMode::Teleport)
                pendingTeleportAck = true;
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
            }
            RespawnPacket reply;
            reply.position = p.position;
            reply.state = 2;
            reply.runtimeEntityId = runtimeId;
            Send(reply);
        });

        On<CorrectPlayerMovePredictionPacket>([this](const CorrectPlayerMovePredictionPacket& p) {
            if (p.predictionType != CorrectPlayerMovePredictionPacketPredictionType::Player)
                return;
            if (!SaneWorldPos(p.position.x, p.position.y, p.position.z))
                return;
            pose.x = p.position.x;
            pose.y = p.position.y - kPlayerEyeHeight;
            pose.z = p.position.z;
            haveLastAuthPos = false;
        });

        On<UpdateAbilitiesPacket>([this](const UpdateAbilitiesPacket& p) {
            mayFly = false;
            isFlying = false;
            for (const AbilityLayers& layer : p.abilities) {
                const bool hasMayFly = (layer.allowed & (1u << 10)) != 0;
                const bool flyingNow = (layer.enabled & (1u << 9)) != 0;
                const bool canFlyNow = (layer.enabled & (1u << 10)) != 0;
                if (!hasMayFly || !canFlyNow)
                    continue;
                mayFly = true;
                isFlying = isFlying || flyingNow;
                flySpeed = layer.flySpeed;
                vFlySpeed = layer.verticalFlySpeed;
                walkSpeed = layer.walkSpeed;
            }
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
