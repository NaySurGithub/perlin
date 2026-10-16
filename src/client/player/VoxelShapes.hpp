#pragma once
#include "client/player/PlayerPhysics.hpp"
#include "protocol/Packets.hpp"

#include <string>
#include <vector>

namespace perlin {
    class VoxelShapeRegistry {
    public:
        static VoxelShapeRegistry& Instance();
        void Set(const proto::VoxelShapesPacket& packet);
        void Clear();
        bool Find(const std::string& blockName, std::vector<physics::Box>& out) const;
        size_t Size() const;
    };
}
