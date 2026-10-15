#include "client/ui/Glint.hpp"
#include "util/Env.hpp"
#include "client/renderer/RenderContext.hpp"
#include "client/ui/UiAssets.hpp"

#include <cstdio>
#include <cstdlib>

namespace perlin::ui {
    uint32_t CreateGlintTexture(RenderContext& renderer, int* width) {
        Texture t;
        if (!UiAssets::Instance().Image("textures/misc/enchanted_item_glint", t) || t.pixels.empty())
            return 0;
        if (PERLIN_ENV("PERLIN_GLINT_DEBUG")) std::fprintf(stderr, "[glint] texture %dx%d\n", t.width, t.height);
        bool grey = true;
        for (size_t i = 0; i + 3 < t.pixels.size() && grey; i += 4)
            grey = std::abs(int(t.pixels[i]) - int(t.pixels[i + 1])) < 3 && std::abs(int(t.pixels[i + 1]) - int(t.pixels[i + 2])) < 3;
        if (grey)
            for (size_t i = 0; i + 3 < t.pixels.size(); i += 4) {
                const int b = static_cast<int>(t.pixels[i + 2] * 1.15f);
                t.pixels[i] = static_cast<uint8_t>(t.pixels[i] * 0.71f);
                t.pixels[i + 1] = static_cast<uint8_t>(t.pixels[i + 1] * 0.34f);
                t.pixels[i + 2] = static_cast<uint8_t>(b > 255 ? 255 : b);
            }
        if (width)
            *width = t.width;
        return renderer.UiCreateTexture(t.width, t.height, t.pixels.data(), true, true);
    }
}
