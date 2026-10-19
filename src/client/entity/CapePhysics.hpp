#pragma once
// Cape physics: the cape is a chain of kSegments hinged strips (the 16 parts of the Cape Physics geometry), each a
// damped pendulum. Two independent planes are simulated: pitch (swinging back off the body) and roll (swinging to
// the side). A segment feels
//   - gravity, which pulls it back to hanging straight down in the world (the body's lean is accounted for),
//   - the air: the wind relative to the moving player pushes on the face of the strip, so running lifts the cape and
//     falling flares it,
//   - the inertia of the shoulders speeding up, slowing down or turning,
//   - cloth stiffness towards its neighbours, which is what turns a pendulum into a ribbon that bends,
//   - a flutter that travels down the cape in step with the stride.
// The body is a wall: a segment cannot swing into the back. Everything is in degrees at the interface.
#include <algorithm>
#include <array>
#include <cmath>

namespace perlin {
    struct CapeInput {
        float forward = 0.0f;   // blocks/s, along the body's facing
        float right = 0.0f;     // blocks/s, to the body's right
        float up = 0.0f;        // blocks/s
        float yawRate = 0.0f;   // deg/s, positive turns right
        float lean = 0.0f;      // degrees the torso leans forward (sneaking, swimming)
        float stride = 0.0f;    // walk phase in radians
        float moving = 0.0f;    // 0..1, how much the player is walking
    };

    class CapeChain {
    public:
        static constexpr int kSegments = 16;
        std::array<float, kSegments> pitch{}; // degrees, from the body's down direction, positive swings back
        std::array<float, kSegments> roll{};  // degrees, positive swings to the right

        void Reset() { *this = CapeChain(); }

        void Step(const CapeInput& in, float dt) {
            dt = std::clamp(dt, 0.0f, 0.05f);
            constexpr float kSub = 1.0f / 120.0f;
            int n = std::max(1, static_cast<int>(std::ceil(dt / kSub)));
            const float h = dt / static_cast<float>(n);
            if (!init_) {
                init_ = true;
                fwd_ = in.forward;
                rgt_ = in.right;
                upv_ = in.up;
            }
            // Smooth the measured velocity (it comes from per-frame position differences) and take the acceleration
            // from the smoothed value, so frame time jitter does not shake the cape.
            const float k = 1.0f - std::exp(-dt / 0.06f);
            const float pf = fwd_, pr = rgt_, pu = upv_;
            fwd_ += (in.forward - fwd_) * k;
            rgt_ += (in.right - rgt_) * k;
            upv_ += (in.up - upv_) * k;
            const float inv = dt > 1e-4f ? 1.0f / dt : 0.0f;
            const float aF = std::clamp((fwd_ - pf) * inv, -40.0f, 40.0f);
            const float aR = std::clamp((rgt_ - pr) * inv, -40.0f, 40.0f);
            const float aU = std::clamp((upv_ - pu) * inv, -40.0f, 40.0f);

            const float rad = 0.017453292f;
            const float lean = in.lean * rad;
            // The back pivot swings sideways when the body turns (it sits behind the turning axis).
            const float turnSide = 0.30f * in.yawRate * rad;
            const float windF = std::clamp(fwd_, -9.0f, 9.0f);
            const float windR = std::clamp(rgt_ - turnSide, -9.0f, 9.0f);
            const float windU = std::clamp(upv_, -12.0f, 10.0f);
            const float flutter = in.moving;

            for (int s = 0; s < n; s++) {
                step(pitch, pv_, h, [&](int i, float phi) {
                    const float world = phi + lean;
                    const float sw = std::sin(world), cw = std::cos(world);
                    const float kw = kWind * (0.75f + 0.5f * static_cast<float>(i) / (kSegments - 1));
                    float a = -kGravity * sw;
                    // A strip is never perfectly flat to the airflow, so falling lifts it off the back even from rest.
                    a += kw * (windF * cw - windU * std::sin(world + 0.14f)); // air pushing on the strip
                    a += kInertia * (aF * cw - aU * sw);         // shoulders accelerating
                    a += kFlutter * flutter * std::sin(in.stride - kPhase * static_cast<float>(i));
                    return a;
                }, true);
                step(roll, rv_, h, [&](int i, float psi) {
                    const float sw = std::sin(psi), cw = std::cos(psi);
                    const float kw = kWind * (0.75f + 0.5f * static_cast<float>(i) / (kSegments - 1));
                    float a = -kGravity * sw;
                    a += kw * 0.6f * (windR * -cw);
                    a += kInertia * (-aR * cw);
                    a += kFlutter * 0.35f * flutter * std::sin(in.stride * 0.5f - kPhase * static_cast<float>(i));
                    return a;
                }, false);
            }
        }

    private:
        static constexpr float kGravity = 42.0f;   // rad/s^2
        static constexpr float kWind = 5.2f;
        static constexpr float kInertia = 1.1f;
        static constexpr float kFlutter = 20.0f;
        static constexpr float kPhase = 0.42f;
        static constexpr float kDamping = 5.5f;
        static constexpr float kCloth = 95.0f;     // between segments
        static constexpr float kHinge = 160.0f;    // top segment to the shoulders
        bool init_ = false;
        float fwd_ = 0, rgt_ = 0, upv_ = 0;
        std::array<float, kSegments> pv_{}, rv_{};

        // One semi-implicit Euler step of the chain. `angle` is in degrees, the model works in radians.
        template <typename Torque>
        static void step(std::array<float, kSegments>& angle, std::array<float, kSegments>& vel, float h, Torque&& torque, bool wall) {
            constexpr float rad = 0.017453292f, deg = 57.29578f;
            std::array<float, kSegments> a{};
            for (int i = 0; i < kSegments; i++)
                a[i] = angle[i] * rad;
            std::array<float, kSegments> acc{};
            for (int i = 0; i < kSegments; i++) {
                float t = torque(i, a[i]);
                const float up = i == 0 ? 0.0f : a[i - 1];
                t += (i == 0 ? kHinge : kCloth) * (up - a[i]);
                if (i + 1 < kSegments)
                    t += kCloth * (a[i + 1] - a[i]);
                t -= kDamping * vel[i];
                acc[i] = t;
            }
            for (int i = 0; i < kSegments; i++) {
                vel[i] += acc[i] * h;
                a[i] += vel[i] * h;
                if (wall && a[i] < 0.0f) { // the back of the player
                    a[i] = 0.0f;
                    vel[i] = std::max(vel[i], 0.0f) * 0.2f;
                }
                const float limit = wall ? 1.75f : 0.9f;
                if (a[i] > limit) {
                    a[i] = limit;
                    vel[i] = std::min(vel[i], 0.0f);
                } else if (a[i] < -limit) {
                    a[i] = -limit;
                    vel[i] = std::max(vel[i], 0.0f);
                }
                angle[i] = a[i] * deg;
            }
        }
    };
}
