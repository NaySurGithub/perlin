#pragma once
// Cape physics, the WaveyCapes algorithm (dev.tr7zw.waveycapes, StickSimulation3d + CustomCapeRenderer), ported from the
// mod's own code for Minecraft 26.3.
//
// The cape is a chain of 16 points, one pixel (1/16 block) apart, that hangs from the first point at the shoulders. Once per
// client tick (20 Hz) the first point is moved by the player's motion and every other point is pulled down by gravity; the
// chain is then pulled back to unit spacing by a few passes of a stick solver, kept out of the body, and limited to a bend of
// 20 degrees per joint. The picture is drawn between two ticks by interpolating the points.
//
// What moves the first point (CapeHolder.simulate in the mod): the cloak lag along the body direction (Java's xCloak - x), a
// kick when the player starts falling, the height change times 6, and the sideways velocity times 2. Falling fast therefore
// throws the cape up behind and over the player, which is where its big flare in a long drop comes from.
//
// This file turns the points into what the model needs: for each of the 16 strips its absolute pitch (degrees backward from
// hanging straight down, including the cape's resting 6 degrees) and roll (degrees to the right).
#include <algorithm>
#include <array>
#include <cmath>

namespace perlin {
    struct CapeTick {
        float cloakX = 0, cloakZ = 0;    // Java's xCloak - x and zCloak - z, in blocks
        float bodyYaw = 0;               // degrees, the body's yaw (yBodyRot)
        float yaw = 0;                   // degrees, the entity's yaw (getYRot)
        float moveX = 0, moveY = 0, moveZ = 0; // position now minus position one tick ago, in blocks
        bool crouching = false;
        bool underwater = false;
    };

    class CapeChain {
    public:
        static constexpr int kSegments = 16;
        std::array<float, kSegments> pitch{}; // degrees, positive swings the cape backwards
        std::array<float, kSegments> roll{};  // degrees, positive swings it to the right

        CapeChain() { Reset(); }

        void Reset() {
            for (int i = 0; i < kSegments; i++) {
                x_[i] = 0.0f;
                y_[i] = -static_cast<float>(i);
                z_[i] = 0.0f;
                px_[i] = x_[i];
                py_[i] = y_[i];
                pz_[i] = z_[i];
            }
            sneaking_ = false;
            Sample(1.0f, false);
        }

        // One client tick of the simulation (PlayerMixin.moveCloakUpdate -> CapeHolder.simulate).
        void Tick(const CapeTick& t) {
            const float rad = 0.017453292f;
            float heightMultiplier = 6.0f;
            const float strafeMultiplier = 2.0f;
            if (t.underwater)
                heightMultiplier *= 2.0f;
            gravity_ = t.underwater ? 2.5f : 25.0f;
            const float yawRad = -t.yaw * rad;
            const float sideways = std::cos(yawRad) * t.moveX - std::sin(yawRad) * t.moveZ;
            const float body = t.bodyYaw * rad;
            const float sn = std::sin(body), cs = -std::cos(body);
            const float fall = std::clamp(-t.moveY * 10.0f, 0.0f, 1.0f);
            const bool crouchStart = t.crouching && !sneaking_;
            const float mx = t.cloakX * sn + t.cloakZ * cs + fall + (crouchStart ? 3.0f : 0.0f);
            const float my = t.moveY * heightMultiplier + (crouchStart ? 1.0f : 0.0f);
            const float mz = -sideways * strafeMultiplier;
            sneaking_ = t.crouching;
            // CapeSimulation.move: only the first point is moved.
            px_[0] = x_[0];
            py_[0] = y_[0];
            pz_[0] = z_[0];
            x_[0] += mx;
            y_[0] += my;
            z_[0] += mz;
            simulate();
        }

        // The strips' angles for a frame, `partial` (0..1) of the way from the previous tick to the latest one.
        void Sample(float partial, bool shoulders) {
            std::array<float, kSegments> lx, ly, lz;
            for (int i = 0; i < kSegments; i++) {
                lx[i] = px_[i] + (x_[i] - px_[i]) * partial;
                ly[i] = py_[i] + (y_[i] - py_[i]) * partial;
                lz[i] = pz_[i] + (z_[i] - pz_[i]) * partial;
            }
            for (int i = 0; i < kSegments; i++) {
                const int j = i == kSegments - 1 ? kSegments - 2 : i; // the last strip follows the one above it
                float back = -(lx[j + 1] - lx[j]);
                float side = lz[j + 1] - lz[j];
                float down = -(ly[j + 1] - ly[j]);
                const float len = std::sqrt(back * back + side * side + down * down);
                if (len > 1e-5f) {
                    back /= len;
                    side /= len;
                    down /= len;
                } else {
                    back = side = 0.0f;
                    down = 1.0f;
                }
                // Direction = Rz(roll) * Rx(pitch) * (0, -1, 0): back = sin(pitch), side = cos(pitch) sin(roll),
                // down = cos(pitch) cos(roll). A strip pointing above the shoulders has a negative cos(pitch).
                const float sg = down < 0.0f ? -1.0f : 1.0f;
                const float c = sg * std::sqrt(side * side + down * down);
                float p = std::atan2(back, c) * 57.29578f;
                float r = std::atan2(side * sg, down * sg) * 57.29578f;
                if (shoulders) // the shoulder pads keep the top of the cape flat against them
                    p *= std::min(1.0f, static_cast<float>(i + 1) / 6.0f);
                pitch[i] = 6.0f + p;
                roll[i] = std::clamp(r, -60.0f, 60.0f);
                // A strip that points straight up flips between +180 and -180: keep the chain continuous.
                if (i > 0) {
                    while (pitch[i] - pitch[i - 1] > 180.0f)
                        pitch[i] -= 360.0f;
                    while (pitch[i] - pitch[i - 1] < -180.0f)
                        pitch[i] += 360.0f;
                }
            }
        }

    private:
        static constexpr int kIterations = 3;
        std::array<float, kSegments> x_{}, y_{}, z_{}, px_{}, py_{}, pz_{};
        float gravity_ = 25.0f;
        bool sneaking_ = false;
        float dirX_ = 0, dirY_ = 0, dirZ_ = 0;

        void direction(int a, int b) {
            dirX_ = x_[a] - x_[b];
            dirY_ = y_[a] - y_[b];
            dirZ_ = z_[a] - z_[b];
            const float len = std::sqrt(dirX_ * dirX_ + dirY_ * dirY_ + dirZ_ * dirZ_);
            if (len < 1.0e-4f) {
                dirX_ = dirY_ = dirZ_ = 0.0f;
            } else {
                dirX_ /= len;
                dirY_ /= len;
                dirZ_ /= len;
            }
        }

        void separate(int a, int b) {
            const float mx = (x_[a] + x_[b]) / 2.0f, my = (y_[a] + y_[b]) / 2.0f, mz = (z_[a] + z_[b]) / 2.0f;
            if (a != 0) {
                x_[a] = mx + dirX_ / 2.0f;
                y_[a] = my + dirY_ / 2.0f;
                z_[a] = mz + dirZ_ / 2.0f;
            }
            x_[b] = mx - dirX_ / 2.0f;
            y_[b] = my - dirY_ / 2.0f;
            z_[b] = mz - dirZ_ / 2.0f;
        }

        void applyGravity() {
            const float g = gravity_ * 0.05f;
            for (int i = 1; i < kSegments; i++) {
                px_[i] = x_[i];
                py_[i] = y_[i];
                pz_[i] = z_[i];
                y_[i] += -1.0f * g; // gravity points straight down
            }
        }

        void preventClipping() {
            for (int i = 1; i < kSegments; i++) {
                if (x_[i] - x_[0] > 0.0f)
                    x_[i] = x_[0];
                const float f = static_cast<float>(i) / static_cast<float>(kSegments);
                const float limit = f * f * 5.0f;
                const float dz = z_[0] - z_[i];
                if (dz > limit)
                    z_[i] = z_[0] - limit;
                if (dz < -limit)
                    z_[i] = z_[0] + limit;
            }
        }

        void applyMotion() {
            for (int it = 0; it < kIterations; it++) {
                for (int i = kSegments - 2; i >= 0; i--) {
                    direction(i, i + 1);
                    separate(i, i + 1);
                }
            }
        }

        void limitLength() {
            for (int i = 0; i < kSegments - 1; i++) {
                direction(i, i + 1);
                x_[i + 1] = x_[i] - dirX_;
                y_[i + 1] = y_[i] - dirY_;
                z_[i + 1] = z_[i] - dirZ_;
            }
        }

        void preventSelfClipping(int maxIterations) {
            int count = 0;
            bool again = true;
            while (again && count < maxIterations) {
                again = false;
                for (int i = 0; i < kSegments; i++) {
                    for (int j = i + 1; j < kSegments; j++) {
                        const float dx = x_[i] - x_[j], dy = y_[i] - y_[j], dz = z_[i] - z_[j];
                        if (dx * dx + dy * dy + dz * dz < 0.99f) {
                            again = true;
                            count++;
                            direction(i, j);
                            separate(i, j);
                        }
                    }
                }
            }
        }

        void swing(int i, float degrees) {
            const float dx = x_[i] - x_[i - 1], dy = y_[i] - y_[i - 1], dz = z_[i] - z_[i - 1];
            const float r = degrees * 0.017453292f;
            x_[i + 1] = std::cos(r) * dx - std::sin(r) * dy + x_[i];
            y_[i + 1] = std::sin(r) * dx + std::cos(r) * dy + y_[i];
            z_[i + 1] = dz + z_[i];
        }

        void preventHardBend(int i, float maxBend) {
            const float ax = x_[i - 1] - x_[i], ay = y_[i - 1] - y_[i];
            const float bx = x_[i - 1] - x_[i + 1], by = y_[i - 1] - y_[i + 1];
            const float dot = ax * bx + ay * by;
            const float cross = ax * by - ay * bx;
            const double angle = std::atan2(static_cast<double>(cross), static_cast<double>(dot)) * 180.0 / 3.141592653589793;
            if (angle < -maxBend)
                swing(i, -maxBend * 2.0f);
            if (angle > maxBend)
                swing(i, maxBend * 2.0f);
        }

        void simulate() { // StickSimulation3d.simulate
            applyGravity();
            preventClipping();
            preventSelfClipping(32);
            applyMotion();
            preventSelfClipping(32);
            for (int i = 1; i < kSegments - 2; i++)
                preventHardBend(i, 20.0f);
            limitLength();
        }
    };
}
