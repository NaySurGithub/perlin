#pragma once
#include "network/packet/UpdateAbilitiesPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<UpdateAbilitiesPacket> {
        static void Write(WriteStream& s, const UpdateAbilitiesPacket& p) {
            s.le64(static_cast<uint64_t>(p.targetPlayerId));
            s.u8(p.playerPermission);
            s.u8(p.commandPermission);
            s.varu32(static_cast<uint32_t>(p.layers.size()));
            for (const AbilityLayer& layer : p.layers) {
                s.le16(layer.type);
                s.le32(layer.abilities);
                s.le32(layer.values);
                s.f32(layer.flySpeed);
                s.f32(layer.verticalFlySpeed);
                s.f32(layer.walkSpeed);
            }
        }
        static bool Read(ReadStream& s, UpdateAbilitiesPacket& p) {
            uint64_t target = 0;
            s.le64(target);
            p.targetPlayerId = static_cast<int64_t>(target);
            s.u8(p.playerPermission);
            s.u8(p.commandPermission);
            uint32_t count = 0;
            if (!s.count(count))
                return false;
            p.layers.clear();
            for (uint32_t i = 0; i < count && s.ok; i++) {
                AbilityLayer layer;
                s.le16(layer.type);
                s.le32(layer.abilities);
                s.le32(layer.values);
                s.f32(layer.flySpeed);
                s.f32(layer.verticalFlySpeed);
                s.f32(layer.walkSpeed);
                p.layers.push_back(layer);
            }
            return s.ok;
        }
    };

}
