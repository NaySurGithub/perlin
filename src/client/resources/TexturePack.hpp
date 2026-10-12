#pragma once
#include "client/resources/TextureLoader.hpp"

#include <memory>
#include <string>

namespace perlin {
    // A resource pack that overrides vanilla textures: a folder, or a .zip/.mcpack read in place (nothing is
    // extracted, so opening even a huge pack takes milliseconds). Paths are pack-relative, "textures/blocks/stone.png".
    class TexturePack {
    public:
        static TexturePack& Instance();

        // `name` is a file or folder inside `packsDir`. Returns false (and sets `error`) when it cannot be used.
        bool Open(const std::string& packsDir, const std::string& name, std::string& error);
        void Close();
        bool Active() const;
        // Decodes `rel` from the pack. False when the pack is closed or does not contain it.
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
