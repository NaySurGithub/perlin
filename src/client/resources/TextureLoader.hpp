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

    bool TextureSetColor(const std::string& jsonText, std::string& image, unsigned char rgba[4]);
    void MakeFlatTexture(const unsigned char rgba[4], int size, Texture& out);
    bool LoadTextureMemory(const unsigned char* data, size_t size, Texture& out);
    // Converts a legacy 2:1 player skin (64x32, or an HD multiple) to the square layout in place, filling the left
    // arm and leg from the right ones. Returns true if it converted anything.
    bool UpgradeLegacySkinTexture(Texture& skin);
}
