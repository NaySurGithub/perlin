#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace perlin::resources {
    // Looks up a bundled file by name ("block_states.json") inside the executable.
    using Provider = bool (*)(const char* name, const uint8_t** data, size_t* size);
    void SetProvider(Provider provider);

    // Reads a bundled resource. Uses the copy embedded in the executable; falls back to <exe dir>/resources/<name>
    // (useful for developing with edited data files, and for tools that do not embed anything).
    bool Read(const std::string& name, std::string& out);
    std::string ReadText(const std::string& name);
}
