#pragma once
#include "network/packet/CorrectPlayerMovePredictionPacket.hpp"

namespace perlin::net {

    template <> struct PacketSerializer<CorrectPlayerMovePredictionPacket> {
        static constexpr uint8_t kVehicleRewind = 1;

        static void Write(WriteStream& s, const CorrectPlayerMovePredictionPacket& p) {
            s.u8(p.predictionType);
            Field(s, p.position);
            Field(s, p.delta);
            s.f32(p.rotationX);
            s.f32(p.rotationY);
            if (p.predictionType == kVehicleRewind) {
                s.boolean(true);
                s.f32(p.vehicleAngularVelocity);
            }
            s.boolean(p.onGround);
            s.varu64(p.tick);
        }
        static bool Read(ReadStream& s, CorrectPlayerMovePredictionPacket& p) {
            s.u8(p.predictionType);
            Field(s, p.position);
            Field(s, p.delta);
            s.f32(p.rotationX);
            s.f32(p.rotationY);
            if (p.predictionType == kVehicleRewind) {
                bool present = false;
                s.boolean(present);
                if (present)
                    s.f32(p.vehicleAngularVelocity);
            }
            s.boolean(p.onGround);
            if (s.more())
                s.varu64(p.tick);
            return s.ok;
        }
    };

}
