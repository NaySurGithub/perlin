#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace perlin {

    // The player's skin and cape as raw RGBA, ready for the login's ClientData.
    struct SkinConfig {
        std::string skinPath;  // png; empty = the default (blank) skin
        std::string capePath;  // png; empty = no cape
        bool slimArms = false; // "Alex" 3px arms instead of "Steve" 4px arms
    };

    struct LoadedSkin {
        int skinWidth = 0, skinHeight = 0;
        std::vector<uint8_t> skinRgba;
        int capeWidth = 0, capeHeight = 0;
        std::vector<uint8_t> capeRgba;
        bool slimArms = false;
        std::string skinId;  // unique per image so servers/clients do not mix up cached skins
        std::string capeId;
        bool hasSkin() const { return !skinRgba.empty(); }
        bool hasCape() const { return !capeRgba.empty(); }
    };

    // Loads and validates the images. Returns false with a readable message in `error` when a file is missing, is not
    // an image, or has a size Minecraft does not accept (skin 64x32, 64x64, 128x128; cape 64x32, 128x64, 256x128).
    // Old 64x32 skins are converted to the modern 64x64 layout (the left limbs are mirrored from the right ones).
    bool LoadSkinConfig(const SkinConfig& config, LoadedSkin& out, std::string& error);

    // Exposed for tests.
    std::vector<uint8_t> UpgradeLegacySkin(const std::vector<uint8_t>& rgba64x32);

}
