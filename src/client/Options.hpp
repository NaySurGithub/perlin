#pragma once
#include "client/input/KeyMapping.hpp"

#include <string>

namespace perlin {
    struct Options {
        int maxFps = 0;      // 0 = uncapped (only used with vsync off)
        bool vsync = false;
        bool eulaAccepted = false;
        int renderDistance = 8;
        float fov = 70.0f;
        float gamma = 0.5f;
        std::string shaderPack;
        float mouseSensitivity = 0.15f;
        bool invertMouse = false;
        std::string server = "127.0.0.1:19132";
        std::string playerName = "Perlin";
        std::string resourcePackDir;
        std::string bedrockSamplesDir;
        bool fancyGraphics = true;
        int cloudRange = 128;
        bool rgss = false;
        bool fovEffects = true; // sprinting/flying/speed change the field of view (Java "FOV Effects")
        int damageTilt = 100;     // hurt camera shake strength: 0 none, 100 vanilla
        bool damageTint = false;  // optional red screen flash; the reference client has none in first person
        bool shadows = true;      // shader packs that provide shadows
        int heldLight = 0;        // dynamic light level of the item in hand (0-15), shader packs only
        int shadowDistance = 0;   // blocks; 0 = the pack's default
        std::string texturePack;  // file or folder name inside <data>/texturepacks; empty = default textures
        KeyBindings keys = DefaultBindings();

        const Binding& GetKey(Action action) const { return keys[static_cast<size_t>(action)]; }
        void SetKey(Action action, Binding binding) { keys[static_cast<size_t>(action)] = binding; }

        static Options Load();
        void Save() const;
    };

    struct GraphicsSettings {
        bool ambientOcclusion = true;
        int biomeBlendRadius = 2;
        bool clouds = true;
        bool fancyClouds = true;
        int cloudRange = 128;
        int mipmapLevels = 4;
        bool cutoutLeaves = true;
        bool rgss = false;

        static GraphicsSettings From(const Options& options) {
            GraphicsSettings g;
            g.cloudRange = options.cloudRange;
            g.rgss = options.rgss;
            if (!options.fancyGraphics) {
                g.ambientOcclusion = false;
                g.biomeBlendRadius = 1;
                g.fancyClouds = false;
                g.cloudRange = 32;
                g.mipmapLevels = 2;
                g.cutoutLeaves = false;
                g.rgss = false;
            }
            return g;
        }
    };
}
