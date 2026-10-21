#include <chrono>
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
        if (e.isItem && e.simulated) {
            // Keep our own smooth simulation unless the server disagrees noticeably.
            if (glm::length(pos - e.pos) < 0.6) {
                e.pos += (pos - e.pos) * 0.35;
                if (onGround)
                    e.vel = glm::dvec3(0.0);
                return;
            }
            e.vel = glm::dvec3(0.0);
        }
        const glm::dvec3 next = e.isPlayer ? glm::dvec3(pos.x, pos.y - kPlayerEye, pos.z) : pos;
        if (e.dying && e.isPlayer && glm::dot(next - e.pos, next - e.pos) > 2.25)
            e.dying = false;
        e.pos = next;
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
        if (e.isItem && e.simulated) {
            const glm::dvec3 target(x ? *x : e.pos.x, y ? *y : e.pos.y, z ? *z : e.pos.z);
            if (glm::length(target - e.pos) < 0.6) {
                e.pos += (target - e.pos) * 0.35;
                if (onGround)
                    e.vel = glm::dvec3(0.0);
                return;
            }
            e.vel = glm::dvec3(0.0);
        }
        if (x) e.pos.x = *x;
        if (y) e.pos.y = *y;
        if (z) e.pos.z = *z;
        if (pitch) e.pitch = *pitch;
        if (yaw) e.yaw = *yaw;
        if (headYaw) e.headYaw = *headYaw;
        e.onGround = onGround;
        e.moveSerial++;
    }

    void EntityTracker::SetVehicle(int64_t riderUid, int64_t vehicleUid) {
        std::lock_guard lock(mutex);
        const auto u = byUnique.find(riderUid);
        if (u == byUnique.end())
            return;
        const auto it = entities.find(u->second);
        if (it != entities.end())
            it->second.vehicleUid = vehicleUid;
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
    PERLIN_ENTITY_SET(SetCrawling(uint64_t rid, bool crawling), e.crawling = crawling)
    PERLIN_ENTITY_SET(SetScale(uint64_t rid, float scale), e.scale = scale)
    PERLIN_ENTITY_SET(SetNameTag(uint64_t rid, const std::string& nameTag), e.nameTag = nameTag)
    PERLIN_ENTITY_SET(SetVariant(uint64_t rid, int variant, int markVariant, int color),
                      e.variant = variant; e.markVariant = markVariant; e.color = color)
    PERLIN_ENTITY_SET(SetHealth(uint64_t rid, float health), e.health = health; if (health <= 0.0f && !e.isItem) e.dying = true; else if (health > 0.0f) e.dying = false)
    PERLIN_ENTITY_SET(SetAttribute(uint64_t rid, const std::string& name, float min, float value, float max),
                      if (e.attributes.size() < 64 || e.attributes.count(name)) (e.attributes[name] = Entity::Attribute{min, value, max}))
    PERLIN_ENTITY_SET(SetTarget(uint64_t rid, int64_t targetUid), e.targetUid = targetUid)
    PERLIN_ENTITY_SET(SetOwner(uint64_t rid, int64_t ownerUid), e.ownerUid = ownerUid)
    PERLIN_ENTITY_SET(Die(uint64_t rid), e.dying = true)
    PERLIN_ENTITY_SET(SetHeld(uint64_t rid, const std::string& item), e.mainHand = item)
    PERLIN_ENTITY_SET(SetOffHand(uint64_t rid, const std::string& item), e.offHand = item)
    PERLIN_ENTITY_SET(SetArmor(uint64_t rid, int slot, const std::string& item), if (slot >= 0 && slot < 4) e.armor[slot] = item)
    PERLIN_ENTITY_SET(Hurt(uint64_t rid), e.hurtSerial++)
    PERLIN_ENTITY_SET(Swing(uint64_t rid), e.swingSerial++)
    PERLIN_ENTITY_SET(SetProperty(uint64_t rid, const std::string& key, float value), e.props[key] = value)
    PERLIN_ENTITY_SET(SetPropertyText(uint64_t rid, const std::string& key, const std::string& text), e.propText[key] = text)
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

    bool EntityTracker::FindUnique(int64_t uid, Entity& out) const {
        std::lock_guard lock(mutex);
        const auto u = byUnique.find(uid);
        if (u == byUnique.end())
            return false;
        const auto it = entities.find(u->second);
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

    namespace {
        double NowMs() {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        constexpr double kPickupMs = 160.0;
    }

    void EntityTracker::SetViewer(uint64_t rid, glm::dvec3 feet) {
        std::lock_guard lock(mutex);
        viewerRuntime = rid;
        viewerFeet = feet;
    }

    void EntityTracker::Pickup(uint64_t rid, uint64_t collector) {
        std::lock_guard lock(mutex);
        const auto it = entities.find(rid);
        if (it == entities.end())
            return;
        if (!it->second.isItem || pickups.count(rid)) {
            if (!it->second.isItem) {
                byUnique.erase(it->second.uniqueId);
                entities.erase(it);
            }
            return;
        }
        pickups[rid] = {collector, NowMs(), it->second.pos};
    }

    void EntityTracker::StepPickupsLocked(std::vector<uint64_t>* finished) const {
        if (pickups.empty())
            return;
        const double now = NowMs();
        for (auto it = pickups.begin(); it != pickups.end();) {
            const auto item = entities.find(it->first);
            if (item == entities.end()) {
                it = pickups.erase(it);
                continue;
            }
            const double t = (now - it->second.startMs) / kPickupMs;
            if (t >= 1.0) {
                byUnique.erase(item->second.uniqueId);
                entities.erase(item);
                it = pickups.erase(it);
                continue;
            }
            glm::dvec3 target = it->second.from;
            const auto collector = entities.find(it->second.collector);
            if (viewerRuntime != 0 && it->second.collector == viewerRuntime)
                target = viewerFeet + glm::dvec3(0.0, 0.5, 0.0);
            else if (collector != entities.end())
                target = collector->second.pos + glm::dvec3(0.0, 0.5, 0.0);
            const double e = t * t;
            item->second.pos = it->second.from + (target - it->second.from) * e;
            ++it;
        }
        (void)finished;
    }

    void EntityTracker::Snapshot(std::vector<Entity>& out) const {
        std::lock_guard lock(mutex);
        StepPickupsLocked(nullptr);
        out.resize(entities.size());
        size_t i = 0;
        for (const auto& [rid, e] : entities)
            out[i++] = e;
    }

    std::vector<Entity> EntityTracker::Snapshot() const {
        std::vector<Entity> out;
        Snapshot(out);
        return out;
    }

    size_t EntityTracker::Count() const {
        std::lock_guard lock(mutex);
        return entities.size();
    }
}

namespace perlin::entity {
    void EntityTracker::SetMotion(uint64_t rid, glm::dvec3 velocity) {
        std::lock_guard lock(mutex);
        const auto it = entities.find(rid);
        if (it == entities.end() || !it->second.isItem)
            return;
        it->second.vel = velocity;
        it->second.simulated = true;
    }

    void EntityTracker::StepItems(const std::function<bool(int, int, int)>& solid) {
        std::lock_guard lock(mutex);
        constexpr double kHalf = 0.125, kHeight = 0.25;
        const auto blocked = [&](double x, double y, double z) {
            for (int bx = static_cast<int>(std::floor(x - kHalf)); bx <= static_cast<int>(std::floor(x + kHalf)); bx++)
                for (int by = static_cast<int>(std::floor(y)); by <= static_cast<int>(std::floor(y + kHeight)); by++)
                    for (int bz = static_cast<int>(std::floor(z - kHalf)); bz <= static_cast<int>(std::floor(z + kHalf)); bz++)
                        if (solid(bx, by, bz))
                            return true;
            return false;
        };
        for (auto& [rid, e] : entities) {
            if (!e.isItem || !e.simulated)
                continue;
            e.vel.y -= 0.04;
            glm::dvec3 p = e.pos;
            bool grounded = false;
            const double ny = p.y + e.vel.y;
            if (blocked(p.x, ny, p.z)) {
                if (e.vel.y < 0.0) {
                    grounded = true;
                    const double top = std::floor(ny) + 1.0;
                    if (!blocked(p.x, top, p.z))
                        p.y = top;
                }
                e.vel.y = 0.0;
            } else {
                p.y = ny;
            }
            const double nx = p.x + e.vel.x;
            if (blocked(nx, p.y, p.z)) e.vel.x = 0.0; else p.x = nx;
            const double nz = p.z + e.vel.z;
            if (blocked(p.x, p.y, nz)) e.vel.z = 0.0; else p.z = nz;
            const double friction = grounded ? 0.98 * 0.6 : 0.98;
            e.vel.x *= friction;
            e.vel.z *= friction;
            e.vel.y *= 0.98;
            if (grounded && std::fabs(e.vel.x) < 1e-3 && std::fabs(e.vel.z) < 1e-3)
                e.vel.x = e.vel.z = 0.0;
            if (glm::dot(p - e.pos, p - e.pos) > 1e-10) {
                e.pos = p;
                e.moveSerial++;
            }
            e.onGround = grounded;
        }
    }
}

namespace perlin::entity {
    void EntityTracker::SetSleeping(uint64_t rid, bool sleeping) {
        std::lock_guard lock(mutex);
        const auto it = entities.find(rid);
        if (it != entities.end())
            it->second.sleeping = sleeping;
    }
}
