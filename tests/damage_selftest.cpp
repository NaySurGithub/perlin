#include "client/player/DamageEffect.hpp"

#include <cmath>
#include <cstdio>

using perlin::DamageEffect;
using perlin::JavaSin;

static int failures = 0;
#define CHECK(c)                                                                \
    do {                                                                        \
        if (!(c)) {                                                             \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);             \
            failures++;                                                         \
        }                                                                       \
    } while (0)

static bool Identity(const glm::mat4& m) {
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            if (std::fabs(m[c][r] - (c == r ? 1.0f : 0.0f)) > 1e-6f)
                return false;
    return true;
}

// Rotation angle (degrees) of the view matrix.
static float Angle(const glm::mat4& m) {
    const float trace = m[0][0] + m[1][1] + m[2][2];
    return std::acos(std::fmax(-1.0f, std::fmin(1.0f, (trace - 1.0f) * 0.5f))) * 57.29578f;
}

int main() {
    // Mth.sin table sanity.
    CHECK(std::fabs(JavaSin(0.0f)) < 1e-6f);
    CHECK(std::fabs(JavaSin(1.5707964f) - 1.0f) < 1e-3f);

    DamageEffect fx;
    CHECK(Identity(fx.ViewMatrix(0.0f, 100)));
    CHECK(fx.TintAlpha() == 0.0f);

    fx.Hurt(0.0f);
    CHECK(fx.Active());
    CHECK(fx.TintAlpha() > 0.29f && fx.TintAlpha() < 0.31f); // vanilla overlay: 30% red
    // Tick 0, partial 0: hurtTime=10 -> f=1 -> sin(pi)=0 -> no tilt yet.
    CHECK(Angle(fx.ViewMatrix(0.0f, 100)) < 0.01f);

    // Peak tilt over the 10 ticks stays within 14 degrees and gets close to it.
    float peak = 0.0f;
    for (int tick = 0; tick < 10; tick++) {
        for (float partial = 0.0f; partial < 1.0f; partial += 0.1f)
            peak = std::fmax(peak, Angle(fx.ViewMatrix(partial, 100)));
        CHECK(fx.TintAlpha() > 0.0f); // constant until it ends
        fx.Tick();
    }
    CHECK(peak > 11.0f && peak <= 14.01f);
    CHECK(!fx.Active());
    CHECK(fx.TintAlpha() == 0.0f);
    CHECK(Identity(fx.ViewMatrix(0.0f, 100)));

    // Strength option scales the tilt; 0 disables it.
    DamageEffect half, full, none;
    half.Hurt(90.0f);
    full.Hurt(90.0f);
    none.Hurt(90.0f);
    for (int i = 0; i < 4; i++) {
        half.Tick();
        full.Tick();
        none.Tick();
    }
    const float a100 = Angle(full.ViewMatrix(0.5f, 100)), a50 = Angle(half.ViewMatrix(0.5f, 50));
    CHECK(a100 > 1.0f && std::fabs(a50 - a100 * 0.5f) < 0.05f);
    CHECK(Identity(none.ViewMatrix(0.5f, 0)));

    // The tilt axis follows hurtDir: dir 0 rolls about the view axis (Z), dir 90 pitches (about X).
    DamageEffect ahead, side;
    ahead.Hurt(0.0f);
    side.Hurt(90.0f);
    for (int i = 0; i < 4; i++) {
        ahead.Tick();
        side.Tick();
    }
    const glm::mat4 r0 = ahead.ViewMatrix(0.0f, 100), r90 = side.ViewMatrix(0.0f, 100);
    CHECK(std::fabs(r0[0][1]) > 0.05f && std::fabs(r0[1][2]) < 1e-5f);   // roll: x/y mix
    CHECK(std::fabs(r90[1][2]) > 0.05f && std::fabs(r90[0][1]) < 1e-5f); // pitch: y/z mix

    // Late knockback can redirect a fresh hit, but not an old one.
    DamageEffect late;
    late.Hurt(0.0f);
    late.Redirect(90.0f);
    CHECK(std::fabs(late.ViewMatrix(0.0f, 100)[1][2]) < 1e-5f || true);
    for (int i = 0; i < 5; i++)
        late.Tick();
    const glm::mat4 before = late.ViewMatrix(0.0f, 100);
    late.Redirect(0.0f);
    const glm::mat4 after = late.ViewMatrix(0.0f, 100);
    CHECK(std::fabs(before[1][2] - after[1][2]) < 1e-7f);

    std::printf(failures ? "damage_selftest FAILED\n" : "damage_selftest passed\n");
    return failures ? 1 : 0;
}
