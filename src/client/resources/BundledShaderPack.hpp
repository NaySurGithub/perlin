#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace perlin {
    const char* BundledShaderMeta();
    bool BundledShaderBytes(const std::string& backend, const std::string& shader, const uint8_t*& data, size_t& size);
}
