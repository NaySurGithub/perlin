#pragma once
#include "client/resources/TextureLoader.hpp"

#include <memory>
#include <string>
#include <vector>

namespace perlin {
    class TexturePack {
    public:
        static TexturePack& Instance();

        bool Open(const std::string& packsDir, const std::string& name, std::string& error);
        bool OpenStack(const std::string& packsDir, const std::vector<std::string>& names, std::string& error);
        void Close();
        bool Active() const;
        bool Load(const std::string& rel, Texture& out) const;
        bool Has(const std::string& rel) const;
        std::string Description() const;

    public:
        struct Impl;

    private:
        TexturePack();
        ~TexturePack();
        std::unique_ptr<Impl> impl;
    };
}
