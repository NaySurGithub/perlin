#pragma once
#include "network/packet/TextPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<TextPacket> {
        static bool HasAuthor(uint8_t type) {
            return type == TextType::Chat || type == TextType::Whisper || type == TextType::Announcement;
        }
        static bool HasParameters(uint8_t type) {
            return type == TextType::Translate || type == TextType::Popup || type == TextType::JukeboxPopup;
        }

        static void Write(WriteStream& s, const TextPacket& p) {
            s.boolean(p.localize);
            s.u8(p.type);
            if (HasAuthor(p.type))
                s.str(p.source);
            s.str(p.message);
            if (HasParameters(p.type)) {
                s.varu32(static_cast<uint32_t>(p.parameters.size()));
                for (const std::string& parameter : p.parameters)
                    s.str(parameter);
            }
            s.str(p.xuid);
            s.str(p.platformChatId);
            s.str(p.filteredMessage);
        }
        static bool Read(ReadStream& s, TextPacket& p) {
            s.boolean(p.localize);
            s.u8(p.type);
            if (HasAuthor(p.type))
                s.str(p.source);
            s.str(p.message);
            if (HasParameters(p.type)) {
                uint32_t count = 0;
                if (!s.count(count))
                    return false;
                p.parameters.assign(count, std::string());
                for (std::string& parameter : p.parameters)
                    s.str(parameter);
            }
            if (s.more())
                s.str(p.xuid);
            if (s.more())
                s.str(p.platformChatId);
            if (s.more())
                s.str(p.filteredMessage);
            return s.ok;
        }
    };

}
