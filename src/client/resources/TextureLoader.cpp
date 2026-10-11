#include "client/resources/TextureLoader.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace perlin {
    bool LoadTexture(const std::string& path, Texture& out) {
        int w = 0, h = 0, channels = 0;
        stbi_uc* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
        if (!data)
            return false;
        out.width = w;
        out.height = h;
        out.pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
        stbi_image_free(data);
        return true;
    }
}
