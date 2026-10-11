#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    struct CommandOutputMessage {
        std::string messageId;
        bool successful = false;
        std::vector<std::string> parameters;
    };

    struct CommandOutputPacket {
        uint32_t originType = 0;
        Uuid originUuid;
        std::string requestId;
        int64_t playerId = 0;
        uint8_t outputType = 0;
        uint32_t successCount = 0;
        std::vector<CommandOutputMessage> messages;
        std::string dataSet;
    };

    PERLIN_PACKET_ID(CommandOutputPacket, 79);

}
