#pragma once
#include "client/ui/HudState.hpp"
#include "client/ui/RmlEmit.hpp"

namespace perlin::ui {

    class JavaRandom {
    public:
        explicit JavaRandom(int64_t seed) { SetSeed(seed); }
        void SetSeed(int64_t seed) { state = (static_cast<uint64_t>(seed) ^ 0x5DEECE66Dull) & kMask; }
        int NextInt(int bound);

    private:
        int Next(int bits);
        static constexpr uint64_t kMask = (1ull << 48) - 1;
        uint64_t state = 0;
    };

    void RegisterHudRenderers(RmlEmitter& emitter, const HudState& state, const UiServices& services);
}
