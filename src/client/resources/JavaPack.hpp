#pragma once
#include "client/resources/TextureLoader.hpp"

#include <string>
#include <vector>

namespace perlin::javapack {
    // A place to look for a Bedrock texture inside a Java Edition resource pack: a file under assets/minecraft/ and, for
    // atlas sheets such as gui/icons.png and gui/widgets.png, the part of it that holds the sprite. The rectangle is in
    // fractions of the image, so a 16x pack and a 512x pack both work.
    struct Source {
        std::string path; // relative to assets/minecraft/, e.g. "textures/gui/icons.png"
        float x0 = 0, y0 = 0, x1 = 1, y1 = 1;
        bool Whole() const { return x0 == 0 && y0 == 0 && x1 == 1 && y1 == 1; }
    };

    // The Java files that can stand in for a Bedrock texture path ("textures/blocks/planks_oak.png",
    // "textures/ui/hotbar_3.png", ...), most likely first. Both the pre-1.13 names (textures/blocks, planks_oak) and
    // the current ones (textures/block, oak_planks, gui/sprites/hud/...) are covered. Empty when nothing is known.
    std::vector<Source> Sources(const std::string& bedrockPath);

    // Cuts the sprite out of the loaded sheet. Returns false for an empty or degenerate rectangle.
    bool Crop(const Texture& sheet, const Source& source, Texture& out);
}
