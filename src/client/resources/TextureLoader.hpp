#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace perlin {
    struct Texture {
        int width = 0;
        int height = 0;
        std::vector<unsigned char> pixels;
    };

    bool LoadTexture(const std::string& path, Texture& out);

    bool LoadTextureMemory(const unsigned char* data, size_t size, Texture& out);
}
