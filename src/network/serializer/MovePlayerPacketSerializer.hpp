#pragma once
#include "network/packet/MovePlayerPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<MovePlayerPacket> {
        static constexpr uint8_t kTeleportMode = 2;

        static void Write(WriteStream& s, const MovePlayerPacket& p) {
            s.varu64(p.runtimeId);
            Field(s, p.position);
            s.f32(p.pitch);
            s.f32(p.yaw);
            s.f32(p.headYaw);
            s.u8(p.mode);
            s.boolean(p.onGround);
            s.varu64(p.ridingRuntimeId);
            if (p.mode == kTeleportMode) {
                s.zz32(p.teleportCause);
                s.zz32(p.teleportSourceType);
            }
            s.varu64(p.tick);
        }
        static bool Read(ReadStream& s, MovePlayerPacket& p) {
            s.varu64(p.runtimeId);
            Field(s, p.position);
            s.f32(p.pitch);
            s.f32(p.yaw);
            s.f32(p.headYaw);
            s.u8(p.mode);
            s.boolean(p.onGround);
            if (s.more())
                s.varu64(p.ridingRuntimeId);
            if (p.mode == kTeleportMode && s.more()) {
                s.zz32(p.teleportCause);
                s.zz32(p.teleportSourceType);
            }
            if (s.more())
                s.varu64(p.tick);
            return s.ok;
        }
    };

}
