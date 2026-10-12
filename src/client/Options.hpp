#pragma once
#include "client/input/KeyMapping.hpp"

#include <string>

namespace perlin {
    struct Options {
        int maxFps = 60;
        bool vsync = true;
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
        KeyBindings keys = DefaultBindings();

        const Binding& GetKey(Action action) const { return keys[static_cast<size_t>(action)]; }
        void SetKey(Action action, Binding binding) { keys[static_cast<size_t>(action)] = binding; }

        static Options Load();
        void Save() const;
    };
}
