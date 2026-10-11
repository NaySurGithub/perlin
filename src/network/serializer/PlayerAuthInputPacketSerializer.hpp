#pragma once
#include "network/packet/PlayerAuthInputPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<PlayerAuthInputPacket> {
        static void Write(WriteStream& s, const PlayerAuthInputPacket& p) {

            s.f32(p.pitch);
            s.f32(p.yaw);
            s.f32(p.eyeX);
            s.f32(p.eyeY);
            s.f32(p.eyeZ);
            s.f32(p.moveX);
            s.f32(p.moveZ);
            s.f32(p.headYaw);

            s.varu32(static_cast<uint32_t>(p.flags.size()));
            for (uint32_t id : p.flags) {
                s.zz32(static_cast<int32_t>(id));
            }

            s.varu32(p.inputMode);
            s.varu32(p.playMode);
            s.zz32(p.interactionModel);
            s.f32(p.interactPitch);
            s.f32(p.interactYaw);
            s.varu64(p.tick);
            s.f32(p.deltaX);
            s.f32(p.deltaY);
            s.f32(p.deltaZ);

            s.u8(0);
            s.u8(0);
            if (p.hasBlockAction) {
                s.u8(1);
                s.varu32(1);
                s.zz32(p.blockActionType);
                s.zz32(p.blockX);
                s.zz32(p.blockY);
                s.zz32(p.blockZ);
                s.zz32(p.blockFace);
            } else {
                s.u8(0);
            }
            s.u8(0);
            s.u8(0);

            s.f32(p.analogX);
            s.f32(p.analogZ);
            s.f32(p.camX);
            s.f32(p.camY);
            s.f32(p.camZ);
            s.f32(p.rawX);
            s.f32(p.rawZ);
        }
        static bool Read(ReadStream&, PlayerAuthInputPacket&) { return false; }
    };

}
