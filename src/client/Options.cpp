#include "client/Options.hpp"
#include "util/Paths.hpp"

#include <algorithm>
#include <fstream>
#include "util/Json.hpp"

#include <map>

namespace perlin {
    namespace {
        constexpr int kOptionsVersion = 3;
    }
    namespace stored {
        struct Stored {
            int maxFps = 0;
            bool vsync = false;
            bool eulaAccepted = false;
            int renderDistance = 0;
            float fov = 0;
            float gamma = 0;
            std::string shaderPack;
            float mouseSensitivity = 0;
            bool invertMouse = false;
            std::string server;
            std::string playerName;
            std::string resourcePackDir;
            std::string bedrockSamplesDir;
            std::string graphics = "fancy";
            int cloudRange = 128;
            bool rgss = false;
            bool fovEffects = true;
            int damageTilt = 100;
            bool damageTint = false;
            std::string texturePack;
            bool shadows = true;
            int heldLight = 0;
            int shadowDistance = 0;
            ZoomSettings zoom;
            int version = 0;
            std::map<std::string, std::string> keys;
            std::string skin;
            std::string cape;
            bool slimArms = false;
            bool onlineLogin = false;
            std::string transport = "auto";
        };
    }

    using stored::Stored;

    namespace {
        std::string SettingsPath() {
            return paths::DataDir() + "/options.json";
        }
    }

    Options Options::Load() {
        Options o;
        const std::string dir = paths::DataDir();
        paths::EnsureDir(dir);
        o.resourcePackDir = dir + "/resourcepacks";
        o.bedrockSamplesDir = dir + "/bedrock-samples";

        std::ifstream file(SettingsPath());
        if (!file.good())
            return o;
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        Stored stored{o.maxFps,
                      o.vsync,
                      o.eulaAccepted,
                      o.renderDistance,
                      o.fov,
                      o.gamma,
                      o.shaderPack,
                      o.mouseSensitivity,
                      o.invertMouse,
                      o.server,
                      o.playerName,
                      o.resourcePackDir,
                      o.bedrockSamplesDir,
                      "fancy",
                      o.cloudRange,
                      o.rgss,
                      o.fovEffects,
                      o.damageTilt,
                      o.damageTint,
                      o.texturePack,
                      o.shadows,
                      o.heldLight,
                      o.shadowDistance,
                      o.zoom,
                      0,
                      {},
                      o.skin,
                      o.cape,
                      o.slimArms,
                      o.onlineLogin,
                      o.transport};
        auto parsed = json::Read<Stored>(text);
        if (!parsed)
            return o;
        stored = std::move(*parsed);

        o.maxFps = stored.maxFps;
        o.vsync = stored.vsync;
        o.eulaAccepted = stored.eulaAccepted;
        o.renderDistance = std::clamp(stored.renderDistance, 2, 128);
        o.fov = std::clamp(stored.fov, 30.0f, 110.0f);
        o.gamma = std::clamp(stored.gamma, 0.0f, 1.0f);
        o.shaderPack = stored.shaderPack;
        o.mouseSensitivity = std::clamp(stored.mouseSensitivity, 0.01f, 2.0f);
        o.invertMouse = stored.invertMouse;
        o.server = stored.server;
        o.playerName = stored.playerName;
        o.skin = stored.skin;
        o.cape = stored.cape;
        o.slimArms = stored.slimArms;
        o.onlineLogin = stored.onlineLogin;
        o.transport = stored.transport == "raknet" || stored.transport == "nethernet" ? stored.transport : "auto";
        o.resourcePackDir = stored.resourcePackDir;
        o.bedrockSamplesDir = stored.bedrockSamplesDir;
        o.fancyGraphics = stored.graphics != "fast";
        o.cloudRange = std::clamp(stored.cloudRange, 2, 128);
        o.rgss = stored.rgss;
        o.fovEffects = stored.fovEffects;
        o.damageTilt = std::clamp(stored.damageTilt, 0, 100);
        o.damageTint = stored.damageTint;
        o.texturePack = stored.texturePack;
        o.shadows = stored.shadows;
        o.heldLight = std::clamp(stored.heldLight, 0, 15);
        o.shadowDistance = std::clamp(stored.shadowDistance, 0, 256);
        o.zoom = stored.zoom;
        o.zoom.inSeconds = std::clamp(o.zoom.inSeconds, 0.0f, 5.0f);
        o.zoom.outSeconds = std::clamp(o.zoom.outSeconds, 0.0f, 5.0f);
        o.zoom.minLevel = std::clamp(o.zoom.minLevel, 0.005f, 1.0f);
        o.zoom.maxLevel = std::clamp(o.zoom.maxLevel, o.zoom.minLevel, 1.0f);
        o.zoom.level = std::clamp(o.zoom.level, o.zoom.minLevel, o.zoom.maxLevel);
        o.zoom.scrollStep = std::clamp(o.zoom.scrollStep, 0.0f, 0.9f);
        o.zoom.smoothing = std::clamp(o.zoom.smoothing, 0.0f, 100.0f);
        if (stored.version < kOptionsVersion) {
            // Options saved by older builds carry the old vsync-on / 60 fps cap defaults; move them to the new ones once.
            o.vsync = Options().vsync;
            o.maxFps = Options().maxFps;
            o.damageTint = Options().damageTint;
        }

        for (size_t i = 0; i < kActionCount; i++) {
            const auto it = stored.keys.find(ActionId(static_cast<Action>(i)));
            if (it == stored.keys.end())
                continue;
            const Binding binding = Binding::Parse(it->second);
            if (binding.device != Binding::Device::None)
                o.keys[i] = binding;
        }
        return o;
    }

    void Options::Save() const {
        Stored stored{maxFps,
                      vsync,
                      eulaAccepted,
                      renderDistance,
                      fov,
                      gamma,
                      shaderPack,
                      mouseSensitivity,
                      invertMouse,
                      server,
                      playerName,
                      resourcePackDir,
                      bedrockSamplesDir,
                      fancyGraphics ? "fancy" : "fast",
                      cloudRange,
                      rgss,
                      fovEffects,
                      damageTilt,
                      damageTint,
                      texturePack,
                      shadows,
                      heldLight,
                      shadowDistance,
                      zoom,
                      kOptionsVersion,
                      {},
                      skin,
                      cape,
                      slimArms,
                      onlineLogin,
                      transport};
        for (size_t i = 0; i < kActionCount; i++)
            stored.keys[ActionId(static_cast<Action>(i))] = keys[i].Serialize();

        std::string out;
        if (glz::write<glz::opts{.prettify = true}>(stored, out))
            return;
        paths::EnsureDir(paths::DataDir());
        std::ofstream(SettingsPath()) << out;
    }
}
