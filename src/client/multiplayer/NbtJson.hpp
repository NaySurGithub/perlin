#pragma once
#include "client/ui/Json.hpp"

#include <cstdint>
#include <vector>

namespace perlin {
    bool NetworkNbtToJson(const std::vector<uint8_t>& bytes, ui::Json& out);
}
