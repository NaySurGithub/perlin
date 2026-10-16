#pragma once
#include "protocol/Packets.hpp"

#include <vector>

namespace perlin {
    size_t RegisterCustomBlocks(const std::vector<proto::BlockPropertiesEntry>& blocks);
}
