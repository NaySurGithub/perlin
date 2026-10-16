#pragma once
#include <memory>
#include <string>
#include <vector>

namespace perlin {
    struct ShaderSetting {
        std::string id, label;
        double min = 0.0, max = 1.0, step = 0.1, def = 0.0;
        bool toggle = false;
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

    private:
        ShaderPack();
        ~ShaderPack();
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
