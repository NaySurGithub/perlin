#pragma once
#include <memory>
#include <string>
#include <vector>

namespace perlin {
    // A runtime shader pack, separate from the executable: a folder or a .zip read in place, holding precompiled bgfx
    // shaders per backend as <backend>/<shader>.bin (backend = dxbc, glsl, spirv, essl or metal) plus an optional
    // pack.json {"name","description"}. Programs the pack does not provide fall back to the built-in shaders.
    class ShaderPack {
    public:
        static ShaderPack& Instance();

        bool Open(const std::string& packsDir, const std::string& name, std::string& error);
        void Close();
        bool Active() const;
        bool HasBackend(const std::string& backend) const;
        // Reads <backend>/<shader>.bin. False when the pack does not contain it.
        bool Read(const std::string& backend, const std::string& shader, std::vector<char>& out) const;
        std::string Description() const;

    private:
        ShaderPack();
        ~ShaderPack();
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
