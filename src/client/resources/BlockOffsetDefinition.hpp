#pragma once

#include "client/player/PerlinBlockOffset.hpp"
#include "client/ui/Json.hpp"

namespace perlin {
    // Canonical BDS description object: x/y/z each contain range {min,max}
    // and an unsigned steps value. Returned ranges use native component units.
    std::optional<physics::bds::BlockRandomOffset> ParseBlockOffsetDefinition(const ui::Json* value);
}
