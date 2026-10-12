#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace perlin {
    // (hurtDuration 10 ticks, sin(f^4 * pi) * 14 degrees * damage tilt strength).
    class DamageEffect {
    public:
        static constexpr float kDuration = 0.5f;
        static constexpr float kMaxRoll = 14.0f;
        static constexpr float kMaxTint = 0.4f;

        void Update(uint32_t hurtCount, float dt, float side) {
            if (!primed) {
                seen = hurtCount;
                primed = true;
            }
            if (hurtCount != seen) {
                seen = hurtCount;
                elapsed = 0.0f;
                active = true;
                sign = side < 0.5f ? -1.0f : 1.0f;
            } else if (active) {
                elapsed += dt;
                if (elapsed >= kDuration)
                    active = false;
            }
        }

        float Remaining() const { return active ? std::clamp(1.0f - elapsed / kDuration, 0.0f, 1.0f) : 0.0f; }

        float Roll(int strength) const {
            const float f = Remaining();
            if (f <= 0.0f || strength <= 0)
                return 0.0f;
            const float k = f * f * f * f;
            return sign * std::sin(k * 3.14159265358979f) * kMaxRoll * (std::min(strength, 100) / 100.0f);
        }

        float TintAlpha() const { return kMaxTint * Remaining(); }

    private:
        bool primed = false;
        bool active = false;
        uint32_t seen = 0;
        float elapsed = 0.0f;
        float sign = 1.0f;
    };
}
