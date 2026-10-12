#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>

namespace perlin {
    // Java's Mth.sin: a 65536 entry table indexed by (int)(x * 10430.378f) & 65535.
    inline float JavaSin(float x) {
        static const struct Table {
            float v[65536];
            Table() {
                for (int i = 0; i < 65536; i++)
                    v[i] = static_cast<float>(std::sin(i * 3.141592653589793 * 2.0 / 65536.0));
            }
        } table;
        return table.v[static_cast<int>(x * 10430.378f) & 65535];
    }

    // Hurt feedback, as Java does it (LivingEntity hurtTime / hurtDuration and GameRenderer.bobHurt):
    //  * a hit sets hurtTime = hurtDuration = 10 and picks hurtDir; every client tick hurtTime drops by one;
    //  * the camera is rotated by  Ry(-hurtDir) * Rz(-sin((hurtTime - partialTick) / 10)^4 * pi) * 14deg * strength) * Ry(hurtDir)
    //    in view space, so the view tips toward/away from the damage source and eases back;
    //  * Java has no full-screen flash for the local player; its red hurt overlay (OverlayTexture, 178/255 alpha
    //    mixed towards red, constant while hurtTime > 0) only tints entity models. The optional tint here applies the
    //    same mix to the whole view for the same 10 ticks.
    class DamageEffect {
    public:
        static constexpr int kDuration = 10;
        static constexpr float kMaxRoll = 14.0f;
        static constexpr float kOverlayAlpha = 1.0f - 178.0f / 255.0f; // red weight of the vanilla overlay

        // New damage event. `hurtDirDegrees` is Java's hurtDir (damage source bearing minus the player's yaw).
        void Hurt(float hurtDirDegrees) {
            hurtTime = kDuration;
            dir = hurtDirDegrees;
            age = 0;
            directed = true;
        }

        // The knockback that tells us where the hit came from can arrive a tick after the hurt event.
        void Redirect(float hurtDirDegrees) {
            if (hurtTime > 0 && age <= 2)
                dir = hurtDirDegrees;
        }

        void Tick() {
            if (hurtTime > 0) {
                hurtTime--;
                age++;
            }
        }

        bool Active() const { return hurtTime > 0; }

        // GameRenderer.bobHurt as a view-space matrix; `strength` is the option, 0 (none) to 100 (vanilla).
        glm::mat4 ViewMatrix(float partialTick, int strength) const {
            float f = static_cast<float>(hurtTime) - partialTick;
            if (f < 0.0f || strength <= 0)
                return glm::mat4(1.0f);
            f /= static_cast<float>(kDuration);
            f = JavaSin(f * f * f * f * 3.14159265f);
            const float tilt = -f * kMaxRoll * (static_cast<float>(strength > 100 ? 100 : strength) / 100.0f);
            const glm::vec3 up(0, 1, 0), forward(0, 0, 1);
            glm::mat4 m(1.0f);
            m = glm::rotate(m, glm::radians(-dir), up);
            m = glm::rotate(m, glm::radians(tilt), forward);
            m = glm::rotate(m, glm::radians(dir), up);
            return m;
        }

        // Vanilla overlay: constant while hurtTime > 0.
        float TintAlpha() const { return hurtTime > 0 ? kOverlayAlpha : 0.0f; }

    private:
        int hurtTime = 0;
        int age = 0;
        float dir = 0.0f;
        bool directed = false;
    };
}
