#pragma once

#include "client/player/PerlinTrig.hpp"
#include "client/player/PerlinFloat.hpp"

#include <algorithm>
#include <cmath>

namespace perlin::physics::bds {
    // Shared relative movement kernel 0x1432674C0. Input lanes and speed are
    // already resolved travel intent; this callback also supports vertical input.
    template<class Trigonometry>
    inline Float3 RelativeMovementWithTrig(Float3 velocity, float yaw, Float3 intent, float speed,
                                         Trigonometry trigonometry) {
        const auto mul = detail::SseMultiply;
        const auto add = detail::SseAdd;
        const float square =
            add(add(mul(intent.x, intent.x), mul(intent.y, intent.y)), mul(intent.z, intent.z));
        if (!(square >= 0.0001f))
            return velocity;
        const float scale = detail::SseDivide(speed, std::max(std::sqrt(square), 1.0f));
        const float sideways = mul(scale, intent.x);
        const float forward = mul(scale, intent.z);
        const auto trig = trigonometry(mul(yaw, std::bit_cast<float>(0x3c8efa35u)));
        velocity.y = add(mul(scale, intent.y), velocity.y);
        velocity.x =
            add(detail::SseSubtract(mul(trig.cosine, sideways), mul(trig.sine, forward)), velocity.x);
        velocity.z = add(add(mul(trig.cosine, forward), mul(trig.sine, sideways)), velocity.z);
        return velocity;
    }

    inline Float3 RelativeMovement(Float3 velocity, float yaw, Float3 intent, float speed) {
        return RelativeMovementWithTrig(velocity, yaw, intent, speed, SinCos);
    }

    // Windows BDS 1.26.52.3, 0x14845B3C0: separate imports sinf/cosf,
    // not the bundled 1.26.10 sincos wrapper retained above for its oracle.
    inline Float3 CurrentRelativeMovement(Float3 velocity, float yaw, Float3 intent, float speed) {
        return RelativeMovementWithTrig(velocity, yaw, intent, speed, [](float angle) {
            return SineCosine{std::sin(angle), std::cos(angle)};
        });
    }
}
