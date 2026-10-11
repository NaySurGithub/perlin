#include "network/custom/PlayerAuthInput.hpp"

namespace perlin::proto {

    void Write(net::WriteStream& s, const PlayerAuthInputPacket& in) {
        PlayerAuthInputPacket v = in;
        s.le(v.pitch);
        s.le(v.yaw);
        s.le(v.eyeX);
        s.le(v.eyeY);
        s.le(v.eyeZ);
        s.le(v.moveX);
        s.le(v.moveZ);
        s.le(v.headYaw);
        uint32_t count = static_cast<uint32_t>(v.flags.size());
        s.varint(count);
        for (uint32_t id : v.flags) {
            int32_t signedId = static_cast<int32_t>(id);
            s.zigzag(signedId);
        }
        s.varint(v.inputMode);
        s.varint(v.playMode);
        s.zigzag(v.interactionModel);
        s.le(v.interactPitch);
        s.le(v.interactYaw);
        s.varint(v.tick);
        s.le(v.deltaX);
        s.le(v.deltaY);
        s.le(v.deltaZ);
        uint8_t zero = 0, one = 1;
        s.u8(zero);
        s.u8(zero);
        if (v.hasBlockAction) {
            uint32_t actions = 1;
            s.u8(one);
            s.varint(actions);
            s.zigzag(v.blockActionType);
            s.zigzag(v.blockX);
            s.zigzag(v.blockY);
            s.zigzag(v.blockZ);
            s.zigzag(v.blockFace);
        } else {
            s.u8(zero);
        }
        s.u8(zero);
        s.u8(zero);
        s.le(v.analogX);
        s.le(v.analogZ);
        s.le(v.camX);
        s.le(v.camY);
        s.le(v.camZ);
        s.le(v.rawX);
        s.le(v.rawZ);
    }

    bool Read(net::ReadStream& s, PlayerAuthInputPacket&) {
        s.ok = false;
        return false;
    }

}
