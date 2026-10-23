#include "client/resources/ServerBlocks.hpp"

namespace perlin {
    ServerBlocks& ServerBlocks::Instance() {
        static ServerBlocks instance;
        return instance;
    }

    void ServerBlocks::Add(CustomBlockDef def) {
        std::lock_guard lock(mutex);
        all.push_back(def);
        pending.push_back(std::move(def));
    }

    std::vector<CustomBlockDef> ServerBlocks::Take() {
        std::lock_guard lock(mutex);
        std::vector<CustomBlockDef> out;
        out.swap(pending);
        return out;
    }

    bool ServerBlocks::Pending() const {
        std::lock_guard lock(mutex);
        return !pending.empty();
    }

    void ServerBlocks::Requeue() {
        std::lock_guard lock(mutex);
        pending = all;
    }

    void ServerBlocks::Reset() {
        std::lock_guard lock(mutex);
        pending.clear();
        all.clear();
        collisions.clear();
        collisionRevision.fetch_add(1, std::memory_order_release);
    }

    void ServerBlocks::SetCollision(const std::string& name, CustomBlockCollision collision) {
        std::lock_guard lock(mutex);
        collisions[name] = std::move(collision);
        collisionRevision.fetch_add(1, std::memory_order_release);
    }

    bool ServerBlocks::Collision(const std::string& name, CustomBlockCollision& out) const {
        std::lock_guard lock(mutex);
        const auto it = collisions.find(name);
        if (it == collisions.end())
            return false;
        out = it->second;
        return true;
    }

    void ServerBlocks::SetItem(CustomItemDef item) {
        std::lock_guard lock(mutex);
        const std::string key = item.name;
        items[key] = std::move(item);
    }

    bool ServerBlocks::Item(const std::string& name, CustomItemDef& out) const {
        std::lock_guard lock(mutex);
        const auto it = items.find(name);
        if (it == items.end())
            return false;
        out = it->second;
        return true;
    }

    void ServerBlocks::ClearItems() {
        std::lock_guard lock(mutex);
        items.clear();
    }
}
