#pragma once
#include "client/resources/TextureLoader.hpp"

#include <memory>
#include <string>

namespace perlin {
    class TexturePack {
    public:
        static TexturePack& Instance();

        bool Open(const std::string& packsDir, const std::string& name, std::string& error);
        void Close();
        bool Active() const;
        bool Load(const std::string& rel, Texture& out) const;
        bool Has(const std::string& rel) const;
        std::string Description() const;

    private:
        TexturePack();
        ~TexturePack();
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
