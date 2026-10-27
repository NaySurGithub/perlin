#include "client/player/PlayerFov.hpp"

#include <algorithm>
#include <cmath>

namespace perlin {
    float MovementFovTarget(float movementSpeed, float walkSpeed, bool flying,
                            int slownessAmplifier) {
        // Minecraft Windows 26.52, LocalPlayer modifier 0x144762630.
        // Preserve scalar operation order; do not multiply sprint a second time.
        if (!std::isfinite(movementSpeed) || !std::isfinite(walkSpeed) || walkSpeed <= 0.0f)
            return 1.0f;
        float movement;
        if (slownessAmplifier >= 0) {
            movement = std::max(0.01f, 1.0f + static_cast<float>(slownessAmplifier) * -0.1f);
        } else {
            movement = movementSpeed / walkSpeed;
            movement *= 1.2f;
            movement += 1.0f;
            movement *= 0.5f;
        }
        const float target = (flying ? 1.1f : 1.0f) * movement;
        return std::isfinite(target) ? target : 1.0f;
    }

    void PlayerFov::Tick(float target) {
        // LevelRendererPlayer tick 0x1446a8295: one half-step per 20 Hz tick.
        previous = current;
        if (!std::isfinite(target)) target = 1.0f;
        current += (target - current) * 0.5f;
    }

    float PlayerFov::Sample(float partialTick) const {
        // LevelRendererPlayer::getFov 0x1446bd31c.
        return previous + (current - previous) * std::clamp(partialTick, 0.0f, 1.0f);
    }
}
