#include "client/multiplayer/BedrockSession.hpp"
#include "network/PacketIds.hpp"
#include "network/Packets.hpp"
#include "world/level/block/BlockPalette.hpp"
#include <cmath>
#include <cctype>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
#include <iostream>

namespace perlin {
    using namespace net;

    namespace {
        constexpr uint8_t kPackHaveAll = 3;
        constexpr uint8_t kPackStackFinished = 4;

        bool PrintableAscii(const std::string& s, bool allowEmpty = false) {
            if (s.empty())
                return allowEmpty;
            if (s.size() > 64)
                return false;
            for (unsigned char c : s)
                if (c < 32 || c > 126)
                    return false;
            return true;
        }

        bool TryParseGameRules(const std::vector<uint8_t>& t, bool hardcore, bool editor,
                               std::vector<std::pair<std::string, int>>& rules) {
            wire::Reader r(t.data(), t.size());
            uint64_t seed = 0;
            uint16_t biomeType = 0;
            std::string biome, eduProd;
            int32_t dim = 0, gen = 0, wgm = 0, diff = 0, sx = 0, sz = 0, tmp = 0;
            uint32_t sy = 0;
            uint8_t b = 0, eduFeat = 0;
            float rain = 0, lightning = 0;
            if (!r.ReadLE64(seed) || !r.ReadLE16(biomeType) || !r.ReadString(biome) ||
                !PrintableAscii(biome, true))
                return false;
            if (!r.ReadZigzag32(dim) || !r.ReadZigzag32(gen) || !r.ReadZigzag32(wgm))
                return false;
            if (hardcore && !r.ReadByte(b))
                return false;
            if (!r.ReadZigzag32(diff) || !r.ReadZigzag32(sx) || !r.ReadVarUint32(sy) || !r.ReadZigzag32(sz) ||
                !r.ReadByte(b) || b > 1)
                return false;
            if (editor && !(r.ReadZigzag32(tmp) && r.ReadByte(b) && b <= 1 && r.ReadByte(b) && b <= 1))
                return false;
            if (!r.ReadZigzag32(tmp) || !r.ReadZigzag32(tmp) || !r.ReadByte(eduFeat) || eduFeat > 1 ||
                !r.ReadString(eduProd) || !PrintableAscii(eduProd, true) || !r.ReadLEFloat(rain) ||
                !r.ReadLEFloat(lightning))
                return false;
            if (!(dim >= 0 && dim <= 2 && diff >= 0 && diff <= 3 && rain >= 0.0f && rain <= 1.0f &&
                  lightning >= 0.0f && lightning <= 1.0f))
                return false;
            for (int i = 0; i < 3; i++)
                if (!r.ReadByte(b) || b > 1)
                    return false;
            if (!r.ReadZigzag32(tmp) || !r.ReadZigzag32(tmp) || !r.ReadByte(b) || b > 1 || !r.ReadByte(b) ||
                b > 1)
                return false;
            uint32_t n = 0;
            if (!r.ReadVarUint32(n) || n == 0 || n > 500)
                return false;
            for (uint32_t i = 0; i < n; i++) {
                std::string name;
                uint8_t editable = 0;
                uint32_t type = 0;
                if (!r.ReadString(name) || !PrintableAscii(name) || !r.ReadByte(editable) || editable > 1 ||
                    !r.ReadVarUint32(type))
                    return false;
                int value = 0;
                if (type == 1) {
                    if (!r.ReadByte(b) || b > 1)
                        return false;
                    value = b;
                } else if (type == 2) {
                    int32_t v = 0;
                    if (!r.ReadZigzag32(v))
                        return false;
                    value = v;
                } else if (type == 3) {
                    float f = 0;
                    if (!r.ReadLEFloat(f))
                        return false;
                } else {
                    return false;
                }
                rules.emplace_back(name, value);
            }
            return true;
        }

        bool SaneWorldPos(float x, float y, float z) {
            return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::fabs(x) < 100000.0f &&
                   std::fabs(y) < 10000.0f && std::fabs(z) < 100000.0f;
        }
    }

    void BedrockSession::RegisterPacketHandlers() {
        dispatcher.On<NetworkSettingsPacket>([this](NetworkSettingsPacket& p) {
            compressThreshold = p.compressionThreshold;
            compressAlgorithm = p.compressionAlgorithm;
            compressEnabled = true;
            std::cout << "[bedrock] NetworkSettings threshold=" << p.compressionThreshold
                      << " algorithm=" << p.compressionAlgorithm << std::endl;
            if (!SendLogin()) {
                lastError = "failed to send Login";
                state = State::Failed;
            } else {
                state = State::WaitingPlayStatus;
            }
        });

        dispatcher.On<PlayStatusPacket>([this](PlayStatusPacket& p) {
            const int32_t status = p.status;
            std::cout << "[bedrock] PlayStatus " << status << std::endl;
            if (state == State::WaitingPlayStatus) {
                if (status != 0) {
                    lastError = "login rejected, PlayStatus=" + std::to_string(status);
                    state = State::Failed;
                }
            } else if (status == 3) {
                SendSetLocalPlayerAsInitialized();
                state = State::InGame;
                std::cout << "[bedrock] spawned -> InGame" << std::endl;
            }
        });

        dispatcher.On<DisconnectPacket>([this](DisconnectPacket& p) {
            lastError = "server disconnect, reason=" + std::to_string(p.reason) +
                        (p.message.empty() ? "" : " message='" + p.message + "'");
            std::cout << "[bedrock] " << lastError << std::endl;
            state = State::Failed;
        });

        dispatcher.On<ServerToClientHandshakePacket>([this](ServerToClientHandshakePacket&) {
            std::cout << "[bedrock] ServerToClientHandshake -> replying (no encryption)" << std::endl;
            Send(ClientToServerHandshakePacket{});
        });

        dispatcher.On<ResourcePacksInfoPacket>([this](ResourcePacksInfoPacket&) {
            std::cout << "[bedrock] ResourcePacksInfo" << std::endl;
            SendResourcePackResponse(kPackHaveAll);
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
        dispatcher.On<ResourcePackStackPacket>([this](ResourcePackStackPacket&) {
            std::cout << "[bedrock] ResourcePackStack" << std::endl;
            SendResourcePackResponse(kPackStackFinished);
        });
        dispatcher.On<ResourcePacksReadyForValidationPacket>([this](ResourcePacksReadyForValidationPacket&) {
            std::cout << "[bedrock] ResourcePacksReadyForValidation" << std::endl;
            SendResourcePackResponse(kPackHaveAll);
        });

        dispatcher.On<PacketViolationWarningPacket>([](PacketViolationWarningPacket& p) {
            std::cout << "[bedrock] VIOLATION type=" << p.type << " severity=" << p.severity
                      << " packet=" << p.packetId << " (" << PacketName((uint32_t)p.packetId) << ") ctx='"
                      << p.context.substr(0, 200) << "'" << std::endl;
        });

        dispatcher.On<StartGamePacket>([this](StartGamePacket& p) {
            const float x = p.position.x, y = p.position.y, z = p.position.z;
            runtimeId = p.runtimeId;
            if (!pose.known && std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
                std::fabs(x) <= 100000.0f && std::fabs(y) <= 10000.0f && std::fabs(z) <= 100000.0f)
                pose = {x, y, z, p.yaw, p.pitch, p.yaw, false, true};
            if (std::isfinite(x) && std::isfinite(z) && std::fabs(x) <= 100000.0f &&
                std::fabs(z) <= 100000.0f) {
                startPos[0] = x;
                startPos[1] = y;
                startPos[2] = z;
                haveStartPos = true;
            }
            gotSpawn = true;
            std::cout << "[bedrock] StartGame spawn=(" << x << "," << y << "," << z
                      << ") runtime=" << p.runtimeId << std::endl;
            {
                std::vector<std::pair<std::string, int>> rules;
                bool parsed = false;
                for (int v = 0; v < 4 && !parsed; v++) {
                    rules.clear();
                    parsed = TryParseGameRules(p.tail.bytes, (v & 1) != 0, (v & 2) != 0, rules);
                }
                if (parsed) {
                    for (auto& kv : rules) {
                        std::string lower = kv.first;
                        for (auto& c : lower)
                            c = (char)std::tolower((unsigned char)c);
                        if (lower == "dodaylightcycle") {
                            dayCycleRunning = kv.second != 0;
                            std::cout << "[bedrock] doDaylightCycle=" << kv.second << std::endl;
                        }
                    }
                } else {
                    std::cout << "[bedrock] StartGame gamerules not recognised; assuming the day cycle runs"
                              << std::endl;
                }
            }
            SendRequestChunkRadius();
            SendSetLocalPlayerAsInitialized();
            state = State::InGame;
            std::cout << "[bedrock] spawned -> InGame" << std::endl;
        });

        dispatcher.On<SetTimePacket>([this](SetTimePacket& p) {
            static int logged = 0;
            if (logged++ < 5)
                std::cout << "[bedrock] SetTime " << p.time << std::endl;
            NoteTime(p.time);
        });

        dispatcher.On<SetSpawnPositionPacket>([this](SetSpawnPositionPacket& p) {
            const BlockPos& w = p.spawnPosition;
            std::cout << "[bedrock] SetSpawnPosition type=" << p.spawnType << " pos=(" << p.position.x << ","
                      << p.position.y << "," << p.position.z << ") dim=" << p.dimension << " spawn=(" << w.x
                      << "," << w.y << "," << w.z << ")" << std::endl;
            if (w.y < 1000) {
                worldSpawn[0] = w.x;
                worldSpawn[1] = (int)w.y;
                worldSpawn[2] = w.z;
                haveWorldSpawn = true;
            }
        });

        dispatcher.On<UpdateBlockPacket>([this](UpdateBlockPacket& p) {
            lastBlockPos[0] = p.position.x;
            lastBlockPos[1] = (int)p.position.y;
            lastBlockPos[2] = p.position.z;
            haveBlockPos = true;
            if (p.layer == 0)
                netLevel.SetBlock(lastBlockPos[0], lastBlockPos[1], lastBlockPos[2],
                                  BlockPalette::Instance().Intern((int32_t)p.networkId));
        });

        dispatcher.On<MovePlayerPacket>([this](MovePlayerPacket& p) {
            if (runtimeId != 0 && p.runtimeId != runtimeId)
                return;
            runtimeId = p.runtimeId;
            const Vec3& e = p.position;
            if (SaneWorldPos(e.x, e.y, e.z)) {
                pose = {e.x, e.y - kPlayerEyeHeight, e.z, p.yaw, p.pitch, p.headYaw, p.onGround, true};
                haveLastAuthPos = false;
                gotSpawn = true;
                if (p.mode == 2)
                    pendingTeleportAck = true;
            }
        });

        dispatcher.On<RespawnPacket>([this](RespawnPacket& p) {
            std::cout << "[bedrock] Respawn pos=(" << p.position.x << "," << p.position.y << ","
                      << p.position.z << ") state=" << (int)p.state << std::endl;
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
            reply.runtimeId = runtimeId;
            Send(reply);
        });

        dispatcher.On<CorrectPlayerMovePredictionPacket>([this](CorrectPlayerMovePredictionPacket& p) {
            if (p.predictionType != 0)
                return;
            if (!SaneWorldPos(p.position.x, p.position.y, p.position.z))
                return;
            pose.x = p.position.x;
            pose.y = p.position.y - kPlayerEyeHeight;
            pose.z = p.position.z;
            haveLastAuthPos = false;
        });

        dispatcher.On<UpdateAbilitiesPacket>([this](UpdateAbilitiesPacket& p) {
            mayFly = false;
            isFlying = false;
            for (const AbilityLayer& l : p.layers) {
                const bool hasMayFly = (l.abilities & (1u << 10)) != 0;
                const bool flyingNow = (l.values & (1u << 9)) != 0;
                const bool canFlyNow = (l.values & (1u << 10)) != 0;
                if (hasMayFly && canFlyNow) {
                    mayFly = true;
                    if (flyingNow)
                        isFlying = true;
                    flySpeed = l.flySpeed;
                    vFlySpeed = l.verticalFlySpeed;
                    walkSpeed = l.walkSpeed;
                }
            }
        });

        dispatcher.On<NetworkStackLatencyPacket>([this](NetworkStackLatencyPacket& p) {
            if (p.needResponse)
                SendTickSync(p.timestamp);
        });

        dispatcher.On<CommandOutputPacket>([this](CommandOutputPacket& p) {
            std::cout << "[bedrock] CommandOutput success=" << p.successCount
                      << " msgs=" << p.messages.size();
            for (size_t i = 0; i < p.messages.size() && i < 4; i++) {
                std::cout << " [" << p.messages[i].messageId;
                for (size_t j = 0; j < p.messages[i].parameters.size() && j < 4; j++)
                    std::cout << " '" << p.messages[i].parameters[j].substr(0, 40) << "'";
                std::cout << "]";
            }
            std::cout << std::endl;
        });

        dispatcher.OnRaw(PacketId::LevelChunk, "LevelChunk", [this](const uint8_t* d, size_t n) {
            HandleLevelChunk(std::vector<uint8_t>(d, d + n));
        });
        dispatcher.OnRaw(PacketId::SubChunk, "SubChunk", [this](const uint8_t* d, size_t n) {
            HandleSubChunk(std::vector<uint8_t>(d, d + n));
        });
    }

}
