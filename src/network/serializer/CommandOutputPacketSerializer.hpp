#pragma once
#include "network/packet/CommandOutputPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<CommandOutputPacket> {
        static constexpr uint8_t kDataSetOutput = 4;
        static bool HasPlayerId(uint32_t originType) { return originType == 3 || originType == 4; }

        static void Write(WriteStream& s, const CommandOutputPacket& p) {
            s.varu32(p.originType);
            Field(s, p.originUuid);
            s.str(p.requestId);
            if (HasPlayerId(p.originType))
                s.zz64(p.playerId);
            s.u8(p.outputType);
            s.varu32(p.successCount);
            Field(s, p.messages);
            if (p.outputType == kDataSetOutput)
                s.str(p.dataSet);
        }
        static bool Read(ReadStream& s, CommandOutputPacket& p) {
            s.varu32(p.originType);
            Field(s, p.originUuid);
            s.str(p.requestId);
            if (HasPlayerId(p.originType))
                s.zz64(p.playerId);
            s.u8(p.outputType);
            s.varu32(p.successCount);
            Field(s, p.messages);
            if (p.outputType == kDataSetOutput && s.more())
                s.str(p.dataSet);
            return s.ok;
        }
    };

}
