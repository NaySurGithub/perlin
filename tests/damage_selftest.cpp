#include "client/player/DamageEffect.hpp"

#include <cmath>
#include <cstdio>

using perlin::DamageEffect;

static int failures = 0;
#define CHECK(c)                                                                \
    do {                                                                        \
        if (!(c)) {                                                             \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);             \
            failures++;                                                         \
        }                                                                       \
    } while (0)

int main() {
    DamageEffect fx;
    fx.Update(0, 0.016f, 0.0f);
    CHECK(fx.Remaining() == 0.0f);
    CHECK(fx.Roll(100) == 0.0f);

    fx.Update(1, 0.0f, 1.0f); // a hit
    CHECK(fx.Remaining() > 0.99f);
    CHECK(fx.TintAlpha() > 0.39f);

    float peak = 0.0f;
    for (int i = 0; i < 20; i++) {
        fx.Update(1, 0.02f, 0.0f);
        peak = std::fmax(peak, std::fabs(fx.Roll(100)));
    }
    CHECK(peak > 12.0f && peak <= 14.0f);

    DamageEffect half;
    half.Update(0, 0.0f, 0.0f);
    half.Update(1, 0.0f, 1.0f);
    DamageEffect off = half;
    float peakHalf = 0.0f;
    for (int i = 0; i < 20; i++) {
        half.Update(1, 0.02f, 0.0f);
        off.Update(1, 0.02f, 0.0f);
        peakHalf = std::fmax(peakHalf, std::fabs(half.Roll(50)));
        CHECK(off.Roll(0) == 0.0f);
    }
    CHECK(std::fabs(peakHalf - peak * 0.5f) < 0.5f);

    for (int i = 0; i < 40; i++)
        fx.Update(1, 0.02f, 0.0f);
    CHECK(fx.Remaining() == 0.0f);
    CHECK(fx.TintAlpha() == 0.0f);

    if (failures == 0)
        std::printf("damage_selftest: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
