#include "client/ui/Glint.hpp"
#include "client/renderer/RenderContext.hpp"
#include "client/ui/UiAssets.hpp"

#include <cstdlib>

namespace perlin::ui {
    uint32_t CreateGlintTexture(RenderContext& renderer, int* width) {
        Texture t;
        if (!UiAssets::Instance().Image("textures/misc/enchanted_item_glint", t) || t.pixels.empty())
            return 0;
        bool grey = true;
        for (size_t i = 0; i + 3 < t.pixels.size() && grey; i += 4)
            grey = std::abs(int(t.pixels[i]) - int(t.pixels[i + 1])) < 3 && std::abs(int(t.pixels[i + 1]) - int(t.pixels[i + 2])) < 3;
        if (grey)
            for (size_t i = 0; i + 3 < t.pixels.size(); i += 4) {
                t.pixels[i] = static_cast<uint8_t>(t.pixels[i] * 0.90f);
                t.pixels[i + 1] = static_cast<uint8_t>(t.pixels[i + 1] * 0.45f);
            }
        if (width)
            *width = t.width;
        return renderer.UiCreateTexture(t.width, t.height, t.pixels.data(), true, true);
    }
}
