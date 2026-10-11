#pragma once
#include "network/PacketStream.hpp"

namespace perlin::net {

    namespace TextType {
        constexpr uint8_t Raw = 0;
        constexpr uint8_t Chat = 1;
        constexpr uint8_t Translate = 2;
        constexpr uint8_t Popup = 3;
        constexpr uint8_t JukeboxPopup = 4;
        constexpr uint8_t Tip = 5;
        constexpr uint8_t SystemMessage = 6;
        constexpr uint8_t Whisper = 7;
        constexpr uint8_t Announcement = 8;
    }

    struct TextPacket {
        bool localize = false;
        uint8_t type = 1;
        std::string source;
        std::string message;
        std::vector<std::string> parameters;
        std::string xuid;
        std::string platformChatId;
        std::string filteredMessage;
    };

    PERLIN_PACKET_ID(TextPacket, 9);

}
