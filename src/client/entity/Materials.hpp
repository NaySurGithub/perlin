#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

namespace perlin::entity {
    struct MaterialInfo {
        enum class Blend : uint8_t { Opaque, AlphaTest, Alpha, Additive };
        Blend blend = Blend::AlphaTest;
        bool depthWrite = true;
        bool noCull = false;
        uint8_t emissive = 0;
        bool hurtTint = true;
    };

    class MaterialLibrary {
    public:
        void Load(const std::string& root);
        MaterialInfo Find(const std::string& name) const;
        size_t CustomCount() const { return custom.size(); }
        static MaterialInfo Builtin(const std::string& name);
        static void ApplyJson(const std::string& text, std::unordered_map<std::string, MaterialInfo>& out,
                              const std::unordered_map<std::string, MaterialInfo>& known);

    private:
        std::unordered_map<std::string, MaterialInfo> custom;
    };
}
