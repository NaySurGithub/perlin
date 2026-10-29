// the user shader pack reader is part of the wider client; the headless gpu test runs without packs
#include "client/resources/ShaderPack.hpp"

namespace perlin {
    struct ShaderPack::Impl {};
    ShaderPack::ShaderPack() = default;
    ShaderPack::~ShaderPack() = default;
    ShaderPack& ShaderPack::Instance() {
        static ShaderPack pack;
        return pack;
    }
    bool ShaderPack::Active() const { return false; }
    bool ShaderPack::Bundled() const { return false; }
    bool ShaderPack::Flag(const std::string&, bool fallback) const { return fallback; }
}
