#include "client/input/KeyMapping.hpp"
#include "client/renderer/Lightmap.hpp"
#include "client/renderer/Sky.hpp"

#include <cstdio>
#include <initializer_list>

using namespace perlin;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                          \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

int main() {
    for (int key : std::initializer_list<int>{'A', 'Z', '0', '9', Key::Space, Key::Shift, Key::Escape,
                                              Key::Up, Key::F1, Key::F1 + 4, Key::F12}) {
        const std::string name = KeyName(key);
        CHECK(!name.empty());
        CHECK(KeyFromName(name) == key);
    }

    CHECK(KeyFromName("key.keyboard.nonsense") == 0);
    CHECK(KeyFromName("w") == 0);
    CHECK(MouseFromName(MouseName(Mouse::Right)) == Mouse::Right);

    const KeyBindings defaults = DefaultBindings();
    for (size_t i = 0; i < kActionCount; i++) {
        const Binding& b = defaults[i];
        CHECK(b.device != Binding::Device::None);
        CHECK(Binding::Parse(b.Serialize()) == b);
    }
    CHECK(Binding::Parse("").device == Binding::Device::None);

    const SkyProfile& profile = SkyProfile::Overworld();
    const SkyState noon = ComputeSky(profile, 6000.0f);
    const SkyState midnight = ComputeSky(profile, 18000.0f);
    CHECK(noon.skyDarken > 0.99f);
    CHECK(midnight.skyDarken < 0.21f);
    CHECK(noon.skyColor[2] > midnight.skyColor[2]);
    CHECK(midnight.starBrightness > noon.starBrightness);
    CHECK(noon.sunDirection[1] > 0.99f);
    CHECK(ComputeSky(profile, 30000.0f).skyDarken == noon.skyDarken);
    CHECK(ComputeSky(profile, -6000.0f).skyDarken < 0.3f);

    uint8_t day[kLightmapBytes], night[kLightmapBytes];
    BuildLightmap({1.0f, 0.5f, 0.0f, 0.0f}, day);
    BuildLightmap({0.2f, 0.5f, 0.0f, 0.0f}, night);
    auto texel = [](const uint8_t* map, int sky, int block) {
        return map + (sky * kLightmapSize + block) * 4;
    };
    CHECK(texel(day, 15, 0)[1] > texel(night, 15, 0)[1]);
    CHECK(texel(day, 0, 15)[0] == texel(night, 0, 15)[0]);
    CHECK(texel(day, 15, 15)[0] >= texel(day, 8, 8)[0]);
    CHECK(texel(day, 0, 0)[0] < texel(day, 1, 0)[0]);
    for (int level = 0; level < 15; level++)
        CHECK(texel(day, level, 0)[1] <= texel(day, level + 1, 0)[1]);

    const FogRange fog = TerrainFog(128.0f);
    CHECK(fog.end == 128.0f && fog.start == 128.0f - 12.8f);

    std::printf(failures ? "%d FAILED\n" : "input self-tests passed\n", failures);
    return failures ? 1 : 0;
}
