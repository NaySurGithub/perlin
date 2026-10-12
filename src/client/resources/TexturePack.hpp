#pragma once
#include <string>

namespace perlin {
    // Resolves a texture pack name (a folder, .zip or .mcpack inside `packsDir`) to an extracted folder that
    // contains `textures/`. Archives are unpacked once into `<packsDir>/.cache`. Returns "" when the name is empty
    // or the pack cannot be used; `error` then says why.
    std::string ResolveTexturePack(const std::string& packsDir, const std::string& name, std::string& error);
}
