#include "client/multiplayer/BlockEntities.hpp"

namespace perlin {
    namespace {
        void ReadFace(const ui::Json* node, SignFace& face) {
            if (!node || !node->IsObject())
                return;
            if (const ui::Json* t = node->Find("Text"))
                face.text = t->StringOr("");
            if (const ui::Json* c = node->Find("SignTextColor"))
                face.color = static_cast<uint32_t>(static_cast<int64_t>(c->NumberOr(-16777216.0))) | 0xFF000000u;
            if (const ui::Json* g = node->Find("IgnoreLighting"))
                face.glow = g->NumberOr(0) != 0 || g->BoolOr(false);
        }
    }

    void BlockEntityStore::Set(int x, int y, int z, const ui::Json& nbt) {
        if (!nbt.IsObject())
            return;
        BlockEntity be;
        if (const ui::Json* id = nbt.Find("id"))
            be.id = id->StringOr("");
        if (const ui::Json* c = nbt.Find("color"))
            be.color = static_cast<int>(c->NumberOr(-1));
        const ui::Json* front = nbt.Find("FrontText");
        if (front)
            ReadFace(front, be.front);
        else if (const ui::Json* t = nbt.Find("Text")) {
            be.front.text = t->StringOr("");
            if (const ui::Json* c = nbt.Find("SignTextColor"))
                be.front.color = static_cast<uint32_t>(static_cast<int64_t>(c->NumberOr(-16777216.0))) | 0xFF000000u;
            if (const ui::Json* g = nbt.Find("IgnoreLighting"))
                be.front.glow = g->NumberOr(0) != 0 || g->BoolOr(false);
        }
        ReadFace(nbt.Find("BackText"), be.back);
        if (const ui::Json* w = nbt.Find("IsWaxed"))
            be.waxed = w->NumberOr(0) != 0 || w->BoolOr(false);
        std::lock_guard lock(mutex);
        entities[Key(x, y, z)] = std::move(be);
    }

    bool BlockEntityStore::Get(int x, int y, int z, BlockEntity& out) const {
        std::lock_guard lock(mutex);
        const auto it = entities.find(Key(x, y, z));
        if (it == entities.end())
            return false;
        out = it->second;
        return true;
    }

    void BlockEntityStore::Remove(int x, int y, int z) {
        std::lock_guard lock(mutex);
        entities.erase(Key(x, y, z));
    }

    void BlockEntityStore::RemoveChunk(int cx, int cz) {
        std::lock_guard lock(mutex);
        for (auto it = entities.begin(); it != entities.end();) {
            const int x = static_cast<int>((it->first >> 38) & 0x3FFFFFFu);
            const int z = static_cast<int>((it->first >> 12) & 0x3FFFFFFu);
            const int32_t sx = static_cast<int32_t>(static_cast<uint32_t>(x) << 6) >> 6;
            const int32_t sz = static_cast<int32_t>(static_cast<uint32_t>(z) << 6) >> 6;
            if ((sx >> 4) == cx && (sz >> 4) == cz)
                it = entities.erase(it);
            else
                ++it;
        }
    }

    void BlockEntityStore::Clear() {
        std::lock_guard lock(mutex);
        entities.clear();
    }

    size_t BlockEntityStore::Size() const {
        std::lock_guard lock(mutex);
        return entities.size();
    }
}
