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

    // Loads an image file. When the file is missing and the path is a .png/.tga, the sibling
    // "<name>.texture_set.json" is tried instead (see TextureSetColor).
    bool LoadTexture(const std::string& path, Texture& out);

    // A Bedrock texture set ("<name>.texture_set.json") names the colour image of a texture, or gives a flat
    // [r, g, b(, a)] colour, in place of the image file itself. Parses the set's JSON text: when its colour is
    // an image, `image` is that image's name (relative to the set's folder, no extension); when it is a flat
    // colour, `image` is empty and `rgba` holds it. Returns false when the set holds no usable colour.
    bool TextureSetColor(const std::string& jsonText, std::string& image, unsigned char rgba[4]);
    // Fills `out` with a flat colour tile of the given size.
    void MakeFlatTexture(const unsigned char rgba[4], int size, Texture& out);
    bool LoadTextureMemory(const unsigned char* data, size_t size, Texture& out);
}
