#pragma once
#include "client/input/KeyMapping.hpp"

#include <string>

namespace perlin {
    struct ZoomSettings {
        float inSeconds = 0.2f;
        float outSeconds = 0.2f;
        std::string easing = "smooth";
        float level = 0.25f;
        float minLevel = 0.02f;
        float maxLevel = 1.0f;
        float scrollStep = 0.15f;
        bool resetOnPress = true;
        bool cinematic = true;
        float smoothing = 9.0f;
        bool scaleSensitivity = true;
    };

    struct HandSettings {
        bool show = true;
        bool viewBobbing = true;
        bool bobCamera = true;
        bool bobHand = true;
        float bobStrength = 1.0f;
        bool swing = true;
        float swingSpeed = 1.0f;
        bool equipAnimation = true;
        bool sway = true;
        bool leftHanded = false;
        bool showOffhand = true;
        bool slimArm = false;
        bool bedrockRig = true;
        bool attachables = true;
        float scale = 1.0f;
    };

    struct SoundSettings {
        float master = 1.0f, music = 1.0f, record = 1.0f, weather = 1.0f, block = 1.0f, hostile = 1.0f, neutral = 1.0f,
              player = 1.0f, ambient = 1.0f, voice = 1.0f;
        bool subtitles = false;
    };

    struct Options {
        int maxFps = 0;
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
        std::string skin;
        std::string cape;
        bool slimArms = false;
        std::string transport = "auto";
        bool onlineLogin = false;
        std::string resourcePackDir;
        std::string bedrockSamplesDir;
        bool fancyGraphics = true;
        int cloudRange = 128;
        bool rgss = false;
        float motionBlur = 0.0f;
        bool thirdPersonCrosshair = false;
        std::string glass = "normal";
        bool thirdPersonNametag = false;
        bool fovEffects = true;
        int damageTilt = 100;
        bool damageTint = false;
        bool shadows = true;
        int heldLight = 0;
        int shadowDistance = 0;
        ZoomSettings zoom;
        HandSettings hand;
        int guiScale = 0;
        SoundSettings sound;
        std::string dynamicLights = "off";
        int menuBlur = 5;
        std::string sprintMode = "hold";
        std::string sneakMode = "hold";
        std::string language = "en_US";
        bool showHud = true;
        std::string texturePack;
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
