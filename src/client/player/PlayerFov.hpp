#pragma once

namespace perlin {
    // Resolved native movement attribute: sprint is already included in it.
    float MovementFovTarget(float movementSpeed, float walkSpeed, bool flying,
                            int slownessAmplifier = -1);

    class PlayerFov {
    public:
        void Tick(float target);
        float Sample(float partialTick) const;
    private:
        float previous = 1.0f;
        float current = 1.0f;
    };
}
