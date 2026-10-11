#include "client/Options.hpp"
#include "util/Paths.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace perlin {
    namespace {
        std::string SettingsPath() {
            return paths::DataDir() + "/options.json";
        }
    }

    Options Options::Load() {
        Options o;
        std::string dir = paths::DataDir();
        paths::EnsureDir(dir);
        o.resourcePackDir = dir + "/resourcepacks";
        o.bedrockSamplesDir = dir + "/bedrock-samples";

        std::ifstream file(SettingsPath());
        if (!file.good())
            return o;

        try {
            nlohmann::json j = nlohmann::json::parse(file);
            o.maxFps = j.value("maxFps", o.maxFps);
            o.vsync = j.value("vsync", o.vsync);
            o.eulaAccepted = j.value("eulaAccepted", o.eulaAccepted);
            o.renderDistance = std::clamp(j.value("renderDistance", o.renderDistance), 2, 32);
            o.fov = std::clamp(j.value("fov", o.fov), 30.0f, 110.0f);
            o.gamma = std::clamp(j.value("gamma", o.gamma), 0.0f, 1.0f);
            o.shaderPack = j.value("shaderPack", o.shaderPack);
            o.mouseSensitivity = std::clamp(j.value("mouseSensitivity", o.mouseSensitivity), 0.01f, 2.0f);
            o.invertMouse = j.value("invertMouse", o.invertMouse);
            o.server = j.value("server", o.server);
            o.playerName = j.value("playerName", o.playerName);
            o.resourcePackDir = j.value("resourcePackDir", o.resourcePackDir);
            o.bedrockSamplesDir = j.value("bedrockSamplesDir", o.bedrockSamplesDir);

            if (j.contains("keys") && j["keys"].is_object()) {
                for (size_t i = 0; i < kActionCount; i++) {
                    auto it = j["keys"].find(ActionId(static_cast<Action>(i)));
                    if (it == j["keys"].end() || !it->is_string())
                        continue;
                    Binding b = Binding::Parse(it->get<std::string>());
                    if (b.device != Binding::Device::None)
                        o.keys[i] = b;
                }
            }
        } catch (const nlohmann::json::exception&) {}
        return o;
    }

    void Options::Save() const {
        nlohmann::json j;
        j["maxFps"] = maxFps;
        j["vsync"] = vsync;
        j["eulaAccepted"] = eulaAccepted;
        j["renderDistance"] = renderDistance;
        j["fov"] = fov;
        j["gamma"] = gamma;
        j["shaderPack"] = shaderPack;
        j["mouseSensitivity"] = mouseSensitivity;
        j["invertMouse"] = invertMouse;
        j["server"] = server;
        j["playerName"] = playerName;
        j["resourcePackDir"] = resourcePackDir;
        j["bedrockSamplesDir"] = bedrockSamplesDir;
        nlohmann::json keyJson = nlohmann::json::object();
        for (size_t i = 0; i < kActionCount; i++)
            keyJson[ActionId(static_cast<Action>(i))] = keys[i].Serialize();
        j["keys"] = keyJson;

        paths::EnsureDir(paths::DataDir());
        std::ofstream(SettingsPath()) << j.dump(2);
    }
}
