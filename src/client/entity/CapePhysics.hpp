#pragma once
// Cape physics: the cape is a chain of kSegments hinged strips (the 16 parts of the Cape Physics geometry).
//
// Where the cape wants to be is what Java Edition's cape does (AvatarRenderer.extractCapeState and
// PlayerCapeModel.setupAnim in the client jar): the whole cape swings out by 6 + capeLean/2 + capeFlap degrees, where
// capeLean (0..150) is how far the cloak trails behind the player, capeFlap (-6..32) is the cloak's height lag plus the
// walking bob, and it leans sideways by capeLean2/2 (-10..10). Those come in as `targetPitch` / `targetRoll`.
//
// What this adds is the flexible part: every strip follows that target through its own damped spring, the strips lower
// down respond more slowly, and neighbours are tied together by cloth stiffness. A change in the target (starting to
// run, a jump, a turn) therefore travels down the cape as a wave instead of moving it as a board, and a flutter in step
// with the stride ripples along it. At rest, and while moving steadily, the cape sits exactly at Java's angle.
#include <algorithm>
#include <array>
#include <cmath>

namespace perlin {
    struct CapeInput {
        float targetPitch = 6.0f; // degrees, Java: 6 + capeLean / 2 + capeFlap
        float targetRoll = 0.0f;  // degrees to the right, Java: capeLean2 / 2
        float stride = 0.0f;      // walk phase in radians
        float moving = 0.0f;      // 0..1, how much the player is walking
        bool shoulders = false;   // the shoulder pads are on: the top of the cape stays pinned to them
    };

    class CapeChain {
    public:
        static constexpr int kSegments = 16;
        std::array<float, kSegments> pitch{}; // degrees from the body's down direction, positive swings back
        std::array<float, kSegments> roll{};  // degrees, positive swings to the right

        CapeChain() { Reset(); }

        void Reset() {
            pitch.fill(6.0f);
            roll.fill(0.0f);
            pv_.fill(0.0f);
            rv_.fill(0.0f);
        }

        void Step(const CapeInput& in, float dt) {
            dt = std::clamp(dt, 0.0f, 0.05f);
            constexpr float kSub = 1.0f / 120.0f;
            const int n = std::max(1, static_cast<int>(std::ceil(dt / kSub)));
            const float h = dt / static_cast<float>(n);
            // The target angle for the whole cape; each strip takes its share of it. A cape that hangs from the shoulders
            // is a curve rather than a board: the top follows the back, the hem swings out the furthest, and the shares
            // average to 1, so the cape as a whole sits at the target angle. With the shoulder pads on, the top strip
            // stays nearly pinned so it never pulls away from them.
            const float pitchTarget = std::clamp(in.targetPitch, 0.0f, 150.0f);
            const float rollTarget = std::clamp(in.targetRoll, -20.0f, 20.0f);
            const float pitchBase = in.shoulders ? 0.30f : 0.55f, pitchSlope = in.shoulders ? 1.40f : 0.90f;
            const float rollBase = in.shoulders ? 0.15f : 0.35f, rollSlope = in.shoulders ? 1.70f : 1.30f;
            for (int s = 0; s < n; s++) {
                for (int i = 0; i < kSegments; i++) {
                    const float along = static_cast<float>(i) / (kSegments - 1);
                    // The ripple of the stride: small at the top, growing towards the hem.
                    const float ripple = kFlutter * in.moving * along * std::sin(in.stride - kPhase * static_cast<float>(i));
                    drive_[i] = std::clamp(pitchTarget * (pitchBase + pitchSlope * along), 0.0f, 150.0f) + ripple;
                    driveRoll_[i] = rollTarget * (rollBase + rollSlope * along) + 0.5f * ripple;
                }
                step(pitch, pv_, drive_, h, true);
                step(roll, rv_, driveRoll_, h, false);
            }
        }

    private:
        static constexpr float kFlutter = 7.0f;   // degrees at the hem while walking
        static constexpr float kPhase = 0.45f;    // radians of stride between neighbouring strips
        static constexpr float kTopSpring = 1100.0f;  // 1/s^2, the strip at the shoulders follows quickly
        static constexpr float kHemSpring = 200.0f;   // the hem follows slowly, which is what lets it lag and swing
        static constexpr float kDamping = 0.55f;     // fraction of critical damping
        static constexpr float kCloth = 120.0f;       // stiffness between neighbouring strips
        std::array<float, kSegments> pv_{}, rv_{};
        std::array<float, kSegments> drive_{}, driveRoll_{};

        static void step(std::array<float, kSegments>& angle, std::array<float, kSegments>& vel,
                         const std::array<float, kSegments>& target, float h, bool wall) {
            std::array<float, kSegments> acc{};
            for (int i = 0; i < kSegments; i++) {
                const float along = static_cast<float>(i) / (kSegments - 1);
                const float k = kTopSpring + (kHemSpring - kTopSpring) * along;
                float a = k * (target[i] - angle[i]);
                if (i > 0)
                    a += kCloth * (angle[i - 1] - angle[i]);
                if (i + 1 < kSegments)
                    a += kCloth * (angle[i + 1] - angle[i]);
                a -= 2.0f * kDamping * std::sqrt(k) * vel[i];
                acc[i] = a;
            }
            for (int i = 0; i < kSegments; i++) {
                vel[i] += acc[i] * h;
                angle[i] += vel[i] * h;
                if (wall && angle[i] < 0.0f) { // the back of the player
                    angle[i] = 0.0f;
                    vel[i] = std::max(vel[i], 0.0f) * 0.2f;
                }
                if (angle[i] > 150.0f) {
                    angle[i] = 150.0f;
                    vel[i] = std::min(vel[i], 0.0f);
                }
            }
        }
    };
}
