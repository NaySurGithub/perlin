#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace perlin {
    struct ShaderSetting {
        std::string id, label;
        double min = 0.0, max = 1.0, step = 0.1, def = 0.0;
        bool toggle = false;
        int slot = -1;                    // index into u_packSettings (x/y/z/w of 8 vec4s)
        std::vector<std::string> labels;  // names for each step of a stepped slider ("Off", "Low"...)
        std::string group;                // section header on the settings page
    };

    // A named set of values for pack settings ("settings") and engine options ("engine":
    // shadowQuality, shadowDistance, shadowEntities, bloom).
    struct ShaderPreset {
        std::string name;
        std::map<std::string, double> settings;
        std::map<std::string, double> engine;
    };

    class ShaderPack {
    public:
        static ShaderPack& Instance();

        bool Open(const std::string& packsDir, const std::string& name, std::string& error);
        void Close();
        bool Active() const;
        bool HasBackend(const std::string& backend) const;
        bool Read(const std::string& backend, const std::string& shader, std::vector<char>& out) const;
        std::string Description() const;
        bool Feature(const std::string& name) const;
        bool Flag(const std::string& name, bool fallback) const;
        double Number(const std::string& name, double fallback) const;
        std::vector<ShaderSetting> Settings() const;
        std::vector<ShaderPreset> Presets() const;

    private:
        ShaderPack();
        ~ShaderPack();
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
