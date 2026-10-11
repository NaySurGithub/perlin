#pragma once
#include "network/packet/CommandRequestPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<CommandRequestPacket> {
        static bool HasPlayerId(uint32_t originType) {
            return originType == CommandOrigin::DevConsole || originType == CommandOrigin::Test;
        }

        static void Write(WriteStream& s, const CommandRequestPacket& p) {
            s.str(p.command);
            s.varu32(p.originType);
            Field(s, p.originUuid);
            s.str(p.requestId);
            if (HasPlayerId(p.originType))
                s.zz64(p.playerId);
            s.boolean(p.isInternal);
            s.zz32(p.version);
        }
        static bool Read(ReadStream& s, CommandRequestPacket& p) {
            s.str(p.command);
            s.varu32(p.originType);
            Field(s, p.originUuid);
            s.str(p.requestId);
            if (HasPlayerId(p.originType))
                s.zz64(p.playerId);
            s.boolean(p.isInternal);
            s.zz32(p.version);
            return s.ok;
        }
    };

}
