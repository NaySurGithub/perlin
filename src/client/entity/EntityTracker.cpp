#include "client/entity/EntityTracker.hpp"

namespace perlin::entity {
    void EntityTracker::Clear() {
        std::lock_guard lock(mutex);
        entities.clear();
        byUnique.clear();
    }

    void EntityTracker::AttachSkinLocked(Entity& e) {
        if (!e.isPlayer)
            return;
        const auto it = skins.find(e.uuid);
        if (it != skins.end())
            e.skin = it->second;
    }

    void EntityTracker::AddPlayer(uint64_t rid, int64_t uid, const std::string& uuid, const std::string& name, glm::dvec3 eyePos,
                                  float pitch, float yaw, float headYaw, uint64_t flags, const std::string& mainHand) {
        std::lock_guard lock(mutex);
        Entity& e = entities[rid];
        const bool fresh = e.runtimeId == 0;
        e.runtimeId = rid;
        e.uniqueId = uid;
        e.type = "minecraft:player";
        e.isPlayer = true;
        e.uuid = uuid;
        e.name = name;
        e.pos = {eyePos.x, eyePos.y - kPlayerEye, eyePos.z};
        e.pitch = pitch;
        e.yaw = yaw;
        e.headYaw = headYaw;
        e.flags = flags;
        e.mainHand = mainHand;
        e.isSelf = rid == selfRuntime && rid != 0;
        if (fresh)
            e.moveSerial = 1;
        byUnique[uid] = rid;
        AttachSkinLocked(e);
    }

    void EntityTracker::AddEntity(uint64_t rid, int64_t uid, const std::string& type, glm::dvec3 feetPos, float pitch, float yaw,
                                  float headYaw, uint64_t flags, float scale, int variant, int markVariant, int color) {
        std::lock_guard lock(mutex);
        Entity& e = entities[rid];
        e.runtimeId = rid;
        e.uniqueId = uid;
        e.type = type;
        e.isPlayer = type == "minecraft:player";
        e.pos = feetPos;
        e.pitch = pitch;
        e.yaw = yaw;
        e.headYaw = headYaw;
        e.flags = flags;
        e.scale = scale;
        e.variant = variant;
        e.markVariant = markVariant;
        e.color = color;
        e.moveSerial = 1;
        byUnique[uid] = rid;
    }

    void EntityTracker::AddItem(uint64_t rid, int64_t uid, glm::dvec3 pos, const std::string& name, int count, int aux,
                                uint32_t blockRuntimeId, bool enchanted) {
        std::lock_guard lock(mutex);
        Entity& e = entities[rid];
        e.runtimeId = rid;
        e.uniqueId = uid;
        e.type = "minecraft:item";
        e.isItem = true;
        e.pos = pos;
        e.itemName = name;
        e.itemCount = count;
        e.itemAux = aux;
        e.itemBlock = blockRuntimeId;
        e.itemEnchanted = enchanted;
        e.moveSerial = 1;
        byUnique[uid] = rid;
    }

    void EntityTracker::RemoveRuntime(uint64_t rid) {
        std::lock_guard lock(mutex);
        const auto it = entities.find(rid);
        if (it == entities.end())
            return;
        byUnique.erase(it->second.uniqueId);
        entities.erase(it);
    }

    void EntityTracker::Remove(int64_t uid) {
        std::lock_guard lock(mutex);
        const auto it = byUnique.find(uid);
        if (it == byUnique.end())
            return;
        entities.erase(it->second);
        byUnique.erase(it);
    }

    void EntityTracker::Move(uint64_t rid, glm::dvec3 pos, float pitch, float yaw, float headYaw, bool onGround) {
        std::lock_guard lock(mutex);
        const auto it = entities.find(rid);
        if (it == entities.end())
            return;
        Entity& e = it->second;
        e.pos = e.isPlayer ? glm::dvec3(pos.x, pos.y - kPlayerEye, pos.z) : pos;
        e.pitch = pitch;
        e.yaw = yaw;
        e.headYaw = headYaw;
        e.onGround = onGround;
        e.moveSerial++;
    }

    void EntityTracker::MoveDelta(uint64_t rid, const double* x, const double* y, const double* z, const float* pitch,
                                  const float* yaw, const float* headYaw, bool onGround) {
        std::lock_guard lock(mutex);
        const auto it = entities.find(rid);
        if (it == entities.end())
            return;
        Entity& e = it->second;
        if (x) e.pos.x = *x;
        if (y) e.pos.y = *y;
        if (z) e.pos.z = *z;
        if (pitch) e.pitch = *pitch;
        if (yaw) e.yaw = *yaw;
        if (headYaw) e.headYaw = *headYaw;
        e.onGround = onGround;
        e.moveSerial++;
    }

#define PERLIN_ENTITY_SET(name, body)                                                                                  \
    void EntityTracker::name {                                                                                         \
        std::lock_guard lock(mutex);                                                                                   \
        const auto it = entities.find(rid);                                                                            \
        if (it == entities.end())                                                                                      \
            return;                                                                                                    \
        Entity& e = it->second;                                                                                        \
        body;                                                                                                          \
    }
    PERLIN_ENTITY_SET(SetFlags(uint64_t rid, uint64_t flags), e.flags = flags)
    PERLIN_ENTITY_SET(SetScale(uint64_t rid, float scale), e.scale = scale)
    PERLIN_ENTITY_SET(SetVariant(uint64_t rid, int variant, int markVariant, int color),
                      e.variant = variant; e.markVariant = markVariant; e.color = color)
    PERLIN_ENTITY_SET(SetHealth(uint64_t rid, float health), e.health = health; if (health <= 0.0f && !e.isItem) e.dying = true)
    PERLIN_ENTITY_SET(Die(uint64_t rid), e.dying = true)
    PERLIN_ENTITY_SET(SetHeld(uint64_t rid, const std::string& item), e.mainHand = item)
    PERLIN_ENTITY_SET(SetArmor(uint64_t rid, int slot, const std::string& item), if (slot >= 0 && slot < 4) e.armor[slot] = item)
    PERLIN_ENTITY_SET(Hurt(uint64_t rid), e.hurtSerial++)
    PERLIN_ENTITY_SET(Swing(uint64_t rid), e.swingSerial++)
    PERLIN_ENTITY_SET(SetProperty(uint64_t rid, const std::string& key, float value), e.props[key] = value)
#undef PERLIN_ENTITY_SET

    void EntityTracker::SetSkin(const std::string& uuid, std::shared_ptr<const SkinData> skin) {
        std::lock_guard lock(mutex);
        skins[uuid] = skin;
        for (auto& [rid, e] : entities)
            if (e.isPlayer && e.uuid == uuid)
                e.skin = skin;
    }

    bool EntityTracker::Find(uint64_t rid, Entity& out) const {
        std::lock_guard lock(mutex);
        const auto it = entities.find(rid);
        if (it == entities.end())
            return false;
        out = it->second;
        return true;
    }

    void EntityTracker::SetSelf(uint64_t rid, const std::string& uuid) {
        std::lock_guard lock(mutex);
        selfRuntime = rid;
        selfUuid = uuid;
        const auto it = entities.find(rid);
        if (it != entities.end())
            it->second.isSelf = true;
    }

    std::vector<Entity> EntityTracker::Snapshot() const {
        std::lock_guard lock(mutex);
        std::vector<Entity> out;
        out.reserve(entities.size());
        for (const auto& [rid, e] : entities)
            out.push_back(e);
        return out;
    }

    size_t EntityTracker::Count() const {
        std::lock_guard lock(mutex);
        return entities.size();
    }
}
