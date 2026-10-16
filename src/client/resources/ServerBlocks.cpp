#include "client/resources/ServerBlocks.hpp"

namespace perlin {
    ServerBlocks& ServerBlocks::Instance() {
        static ServerBlocks instance;
        return instance;
    }

    void ServerBlocks::Add(CustomBlockDef def) {
        std::lock_guard lock(mutex);
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
}
