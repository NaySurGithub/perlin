#include "network/Packets.hpp"

#include <cstdio>

using namespace perlin::net;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                          \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

int main() {
    {
        SetTimePacket p;
        p.time = -5000;
        SetTimePacket q;
        CHECK(Decode(Encode(p), q) && q.time == -5000);
    }
    {
        CommandRequestPacket p;
        p.command = "/time query daytime";
        p.originUuid = RandomUuidV4();
        const std::vector<uint8_t> body = Encode(p);
        const size_t expected = 1 + p.command.size() + 1 + 16 + 1 + 1 + 1;
        CHECK(body.size() == expected);
        CHECK(body[0] == p.command.size());
        CHECK(body[1 + p.command.size()] == CommandOrigin::Player);
        CommandRequestPacket q;
        CHECK(Decode(body, q));
        CHECK(q.command == p.command && q.originUuid.bytes == p.originUuid.bytes);
        CHECK(q.version == 52 && !q.isInternal);

        p.originType = CommandOrigin::Test;
        p.playerId = -9;
        CommandRequestPacket r;
        CHECK(Decode(Encode(p), r) && r.playerId == -9 && r.originType == CommandOrigin::Test);
    }
    {
        TextPacket p;
        p.type = TextType::Chat;
        p.source = "Steve";
        p.message = "hello";
        TextPacket q;
        const std::vector<uint8_t> body = Encode(p);
        CHECK(body[0] == 0 && body[1] == TextType::Chat);
        CHECK(Decode(body, q) && q.source == "Steve" && q.message == "hello");

        p.type = TextType::Translate;
        p.parameters = {"a", "b"};
        TextPacket r;
        CHECK(Decode(Encode(p), r) && r.parameters.size() == 2 && r.parameters[1] == "b");
    }
    {
        UpdateAbilitiesPacket p;
        p.targetPlayerId = 77;
        AbilityLayer layer;
        layer.abilities = 1u << 10;
        layer.values = 1u << 10;
        layer.flySpeed = 0.05f;
        p.layers.push_back(layer);
        UpdateAbilitiesPacket q;
        CHECK(Decode(Encode(p), q) && q.targetPlayerId == 77 && q.layers.size() == 1);
        CHECK(q.layers[0].abilities == (1u << 10) && q.layers[0].flySpeed == 0.05f);
    }
    {
        PlayerActionPacket p;
        p.runtimeId = 3;
        p.position = {10, 64, -20};
        PlayerActionPacket q;
        CHECK(Decode(Encode(p), q) && q.position.x == 10 && q.position.y == 64 && q.position.z == -20);
    }
    {
        MovePlayerPacket p;
        p.mode = 2;
        p.teleportCause = 3;
        p.tick = 99;
        MovePlayerPacket q;
        CHECK(Decode(Encode(p), q) && q.teleportCause == 3 && q.tick == 99);
    }
    {
        ResourcePackClientResponsePacket p;
        const std::vector<uint8_t> body = Encode(p);
        CHECK(body.size() == 3 && body[0] == 4);
    }
    {
        std::vector<uint8_t> empty;
        ResourcePacksInfoPacket info;
        CHECK(Decode(empty, info));
        NetworkSettingsPacket ns;
        std::vector<uint8_t> raw = {1, 0, 0, 0};
        CHECK(Decode(raw, ns) && ns.compressionThreshold == 1);
    }

    std::printf(failures ? "%d FAILED\n" : "packet self-tests passed\n", failures);
    return failures ? 1 : 0;
}
