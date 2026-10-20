#pragma once
#include <array>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin {
    using CollisionBox = std::array<float, 6>;

    struct CollisionSet {
        bool defined = false;
        std::vector<CollisionBox> boxes;
    };

    struct CustomBlockCollision {
        CollisionSet base;
        std::vector<std::pair<std::string, CollisionSet>> permutations;
        float friction = -1.0f;
    };

    struct CustomBlockDef {
        std::string name;
        std::map<std::string, std::string> faceTexture;
        bool cutout = false;
        std::string json;
        std::string geometry;
    };

    struct CustomItemDef {
        std::string name;
        std::string icon;
        std::string displayName;
    };

    class ServerBlocks {
    public:
        static ServerBlocks& Instance();
        void Add(CustomBlockDef def);
        std::vector<CustomBlockDef> Take();
        bool Pending() const;
        void Requeue();
        void Reset();

        void SetCollision(const std::string& name, CustomBlockCollision collision);
        bool Collision(const std::string& name, CustomBlockCollision& out) const;

        void SetItem(CustomItemDef item);
        bool Item(const std::string& name, CustomItemDef& out) const;
        void ClearItems();

    private:
        mutable std::mutex mutex;
        std::vector<CustomBlockDef> pending;
        std::vector<CustomBlockDef> all;
        std::unordered_map<std::string, CustomBlockCollision> collisions;
        std::unordered_map<std::string, CustomItemDef> items;
    };
}
