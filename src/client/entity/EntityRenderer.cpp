#include "client/entity/EntityRenderer.hpp"
#include "util/Env.hpp"
#include "client/entity/EntityQuery.hpp"
#include "client/hand/HandRenderer.hpp"
#include "client/ui/ItemData.hpp"

#include "client/ui/UiAssets.hpp"
#include "client/ui/McFont.hpp"

#include <chrono>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace perlin::entity {
    using attachable::Animation;
    using attachable::Pose;
    using molang::Value;

    namespace {
        constexpr float kDeg = 3.14159265358979323846f / 180.0f;

        float WrapDegrees(float a) {
            a = std::fmod(a + 180.0f, 360.0f);
            if (a < 0)
                a += 360.0f;
            return a - 180.0f;
        }

        uint32_t PackColor(const glm::vec3& c, float alpha = 1.0f) {
            auto q = [](float x) { return static_cast<uint32_t>(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
            return (q(alpha) << 24) | (q(c.b) << 16) | (q(c.g) << 8) | q(c.r);
        }

        bool Matches(const std::string& pattern, const std::string& bone) {
            if (pattern == "*")
                return true;
            if (!pattern.empty() && pattern.back() == '*')
                return bone.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
            return pattern == bone;
        }
    }

    namespace {
        const float kAttachDrop = PERLIN_ENV("PERLIN_ATTACH_DROP") ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_ATTACH_DROP"))) : -24.0f;
    }

    struct EntityRenderer::View {
        bool init = false;
        glm::dvec3 pos{0.0}, lastPos{0.0};
        float yaw = 0, headYaw = 0, pitch = 0;
        uint32_t moveSerial = 0, hurtSerial = 0, swingSerial = 0;
        float limbSwing = 0, limbAmount = 0;
        float groundSpeed = 0, verticalSpeed = 0;
        float capeFlap = 0;
        glm::dvec3 cloak{0.0};
        float capeBob = 0, walkDist = 0, capeX = 6.0f, capeZ = 0.0f;
        glm::mat4 itemR{1.0f}, itemL{1.0f};
        bool hasItemR = false, hasItemL = false;
        bool attachedItem[2] = {false, false};
        bool fp = false;
        double usingSince = 0;
        bool wasUsing = false;
        float attackTime = 0, hurtTimer = 0, deathTime = 0;
        double born = 0, lastSeen = 0, now = 0;
        float dt = 0;
        float animTime = 0;
        float glideTime = 0, yawSpeed = 0, standAmount = 0, sitAmount = 0, swimAmount = 0, lieAmount = 0, swellAmount = 0;
        molang::Scope scope;
        bool initialised = false;
        struct AnimStamp { double start = 0; uint32_t seen = 0; };
        std::unordered_map<std::string, AnimStamp> animStart;
        uint32_t animEpoch = 0;
        struct CtrlState {
            std::string state;
            double entered = 0;
            bool started = false;
        };
        std::map<std::string, CtrlState> ctrl;
        std::vector<EntityRenderer::Layer> cachedLayers;
        float cachedScale = 1.0f;
        int cacheAge = 1 << 20;
        const Entity* entity = nullptr;
        std::string tag;
        glm::mat4 armR{1.0f}, armL{1.0f};
        bool hasArmR = false, hasArmL = false;
    };

    namespace {
        struct EntityHost final : molang::Host, QueryContext {
            bool Query(const std::string& name, const molang::Args& args, Value& out) override {
                return EvalEntityQuery(*this, name, args, out);
            }

            bool Volatile(const std::string& name) const override {
                return name == "anim_time" || name == "all_animations_finished" || name == "any_animation_finished";
            }

            bool Context(const std::string& name, Value& out) override {
                if (name == "is_first_person") { out = Value::Num(firstPerson ? 1 : 0); return true; }
                if (name == "player_offhand_arm_height") { out = Value::Num(offArmHeight); return true; }
                return false;
            }
        };
    }

    EntityRenderer::EntityRenderer() = default;
    EntityRenderer::~EntityRenderer() = default;

    void EntityRenderer::Initialize(RenderContext& r) {
        renderer = &r;
    }

    glm::dvec3 EntityRenderer::SeatPosition(const Entity& v) const {
        float sx = 0, sy = 0, sz = 0;
        if (!assets.Seat(v.type, sx, sy, sz))
            sy = -0.2f;
        const double yawRad = glm::radians(static_cast<double>(v.yaw));
        const glm::dvec3 forward(-std::sin(yawRad), 0.0, std::cos(yawRad)), side(std::cos(yawRad), 0.0, std::sin(yawRad));
        const bool rails = v.type.find("boat") != std::string::npos || v.type.find("minecart") != std::string::npos;
        const double lift = rails ? 0.1 : 0.0;
        return v.pos + side * static_cast<double>(sx) + forward * static_cast<double>(sz) + glm::dvec3(0.0, sy + lift, 0.0);
    }

    void EntityRenderer::ResolveSeats(std::vector<Entity>& entities) const {
        std::unordered_map<int64_t, size_t> byUid;
        for (size_t i = 0; i < entities.size(); i++)
            byUid[entities[i].uniqueId] = i;
        for (Entity& e : entities) {
            if (e.vehicleUid == 0)
                continue;
            const auto it = byUid.find(e.vehicleUid);
            if (it == byUid.end())
                continue;
            const Entity& v = entities[it->second];
            const bool rails = v.type.find("boat") != std::string::npos || v.type.find("minecart") != std::string::npos;
            e.pos = SeatPosition(v);
            e.flags |= flag::Riding;
            e.onGround = true;
            if (rails)
                e.yaw = v.yaw;
        }
    }

    std::pair<float, float> EntityRenderer::HitBox(const Entity& e) const {
        if (e.isPlayer)
            return {0.6f, e.Has(flag::Sneaking) ? 1.5f : 1.8f};
        if (e.isItem)
            return {0.25f, 0.25f};
        float w, h;
        if (assets.CollisionBox(e.type, w, h))
            return {w, h};
        return {0.6f, 1.0f};
    }

    void EntityRenderer::ClearCaches() {
        if (renderer) {
            for (auto& [path, id] : textures)
                if (id)
                    renderer->UiDestroyTexture(id);
            for (auto& [s, id] : skinTextures)
                if (id)
                    renderer->UiDestroyTexture(id);
            for (auto& [s, id] : capeTextures)
                if (id)
                    renderer->UiDestroyTexture(id);
            for (auto& [tile, id] : particleTiles)
                if (id)
                    renderer->UiDestroyTexture(id);
            if (particleSheet)
                renderer->UiDestroyTexture(particleSheet);
            for (uint32_t m : pendingMeshes)
                renderer->UiDestroyMesh(m);
        }
        particleTiles.clear();
        particleSheet = 0;
        particleSheetTried = false;
        textures.clear();
        skinTextures.clear();
        capeTextures.clear();
        pendingMeshes.clear();
        views.clear();
        skinGeometry.clear();
    }

    void EntityRenderer::Shutdown() {
        ClearCaches();
        renderer = nullptr;
    }

    uint32_t EntityRenderer::TextureFor(const Entity& e, const std::string& key) {
        if (e.isPlayer && e.skin) {
            const SkinData& s = *e.skin;
            if (key == "default" && s.skinWidth > 0) {
                auto& id = skinTextures[s.serial];
                if (!id)
                    id = renderer->UiCreateTexture(s.skinWidth, s.skinHeight, s.skinRgba.data(), false);
                return id;
            }
            if (key == "cape" && s.capeWidth > 0) {
                auto& id = capeTextures[s.serial];
                if (!id)
                    id = renderer->UiCreateTexture(s.capeWidth, s.capeHeight, s.capeRgba.data(), false);
                return id;
            }
        }
        if (e.isPlayer && key == "default") {
            const std::string path = e.skin && e.skin->slim ? "textures/entity/alex" : "textures/entity/steve";
            auto it = textures.find(path);
            if (it == textures.end()) {
                Texture t;
                uint32_t id = 0;
                if (ui::UiAssets::Instance().Image(path, t) && t.width > 0)
                    id = renderer->UiCreateTexture(t.width, t.height, t.pixels.data(), false);
                it = textures.emplace(path, id).first;
            }
            return it->second;
        }
        return 0;
    }

    void EntityRenderer::Track(const Entity& e, View& v, const FrameIn& in) {
        const float dt = std::clamp(in.dt, 0.001f, 0.25f);
        v.dt = dt;
        v.now = in.time;
        if (!v.init) {
            v.init = true;
            v.pos = v.lastPos = e.pos;
            v.yaw = e.yaw;
            v.headYaw = e.headYaw;
            v.pitch = e.pitch;
            v.born = in.time;
            v.moveSerial = e.moveSerial;
            v.hurtSerial = e.hurtSerial;
            v.swingSerial = e.swingSerial;
        }
        const double follow = e.isSelf ? 1.0 : 1.0 - std::exp(-static_cast<double>(dt) / 0.06);
        v.lastPos = v.pos;
        const glm::dvec3 d = e.pos - v.pos;
        v.pos = glm::length(d) > 24.0 ? e.pos : v.pos + d * follow;
        const auto turn = [&](float cur, float target) { return cur + WrapDegrees(target - cur) * static_cast<float>(follow); };
        const float prevYaw = v.yaw;
        v.yaw = turn(v.yaw, e.yaw);
        v.yawSpeed = WrapDegrees(v.yaw - prevYaw) / dt;
        v.headYaw = turn(v.headYaw, e.headYaw);
        v.pitch = v.pitch + (e.pitch - v.pitch) * static_cast<float>(follow);
        if (e.type == "minecraft:arrow" || e.type == "minecraft:thrown_trident") {
            const double len = glm::length(d);
            if (len > 0.04) {
                const float targetYaw = static_cast<float>(std::atan2(-d.x, d.z) * 180.0 / 3.14159265358979);
                const float targetPitch = static_cast<float>(-std::asin(std::clamp(d.y / len, -1.0, 1.0)) * 180.0 / 3.14159265358979);
                const float aim = 1.0f - std::exp(-dt / 0.03f);
                v.yaw = v.yaw + WrapDegrees(targetYaw - v.yaw) * aim;
                v.pitch = v.pitch + (targetPitch - v.pitch) * aim;
            }
            v.headYaw = v.yaw;
            v.yawSpeed = 0.0f;
        }

        const glm::dvec3 step = v.pos - v.lastPos;
        const float horizontal = static_cast<float>(std::sqrt(step.x * step.x + step.z * step.z));
        v.groundSpeed = horizontal / dt;
        v.verticalSpeed = static_cast<float>(step.y) / dt;
        const float perTick = horizontal / (dt * 20.0f);
        const float target = std::min(1.0f, perTick * 4.0f);
        v.limbAmount += (target - v.limbAmount) * (1.0f - std::exp(-dt * 20.0f * 0.4f));
        v.limbSwing += v.limbAmount * dt * 20.0f;
        const float flapTarget = std::clamp(v.groundSpeed / 7.0f, 0.0f, 1.0f) * 0.25f;
        v.capeFlap += (flapTarget - v.capeFlap) * (1.0f - std::exp(-dt * 6.0f));
        {
            if (glm::length(v.cloak - v.pos) > 10.0)
                v.cloak = v.pos;
            const double chase = 1.0 - std::pow(0.75, static_cast<double>(dt) * 20.0);
            v.cloak += (v.pos - v.cloak) * chase;
            const double dx = v.cloak.x - v.pos.x, dy = v.cloak.y - v.pos.y, dz = v.cloak.z - v.pos.z;
            const float rad = v.yaw * 0.017453292f;
            const double sn = std::sin(rad), cs = std::cos(rad);
            float flap = std::clamp(static_cast<float>(dy) * 10.0f, -6.0f, 32.0f);
            float lean = std::max(0.0f, static_cast<float>(dx * sn - dz * cs) * 100.0f);
            lean = std::min(lean, 150.0f);
            float lean2 = std::clamp(static_cast<float>(-dx * cs - dz * sn) * 100.0f, -20.0f, 20.0f);
            const float bobTarget = std::min(1.0f, horizontal * 4.0f);
            v.capeBob += (bobTarget - v.capeBob) * (1.0f - std::exp(-dt * 20.0f * 0.4f));
            v.walkDist += horizontal * 0.6f;
            flap += std::sin(v.walkDist * 6.0f) * 32.0f * v.capeBob;
            if (e.Has(flag::Sneaking))
                flap += 25.0f;
            v.capeX = 6.0f + lean / 2.0f + flap;
            v.capeZ = lean2 / 2.0f;
        }

        if (e.Has(flag::UsingItem) && !v.wasUsing)
            v.usingSince = in.time;
        v.wasUsing = e.Has(flag::UsingItem);
        if (e.swingSerial != v.swingSerial) {
            v.swingSerial = e.swingSerial;
            v.attackTime = 0.0001f;
        }
        if (static const char* forced = PERLIN_ENV("PERLIN_PREVIEW_ATTACK"); forced && e.isSelf)
            v.attackTime = static_cast<float>(std::atof(forced));
        else if (v.attackTime > 0)
            v.attackTime = v.attackTime + dt / 0.3f >= 1.0f ? 0.0f : v.attackTime + dt / 0.3f;
        if (e.hurtSerial != v.hurtSerial) {
            v.hurtSerial = e.hurtSerial;
            v.hurtTimer = 0.5f;
        }
        v.hurtTimer = std::max(0.0f, v.hurtTimer - dt);
        if (e.dying) {
            v.deathTime = std::min(v.deathTime + dt, 1.0f);
            v.hurtTimer = 0.5f;
        }
        else
            v.deathTime = 0.0f;
        const auto ease = [&](float& value, bool on, float seconds) {
            const float step = dt / seconds;
            value = on ? std::min(1.0f, value + step) : std::max(0.0f, value - step);
        };
        ease(v.standAmount, e.Has(flag::Rearing), 0.5f);
        ease(v.sitAmount, e.Has(flag::Sitting), 0.3f);
        ease(v.swimAmount, e.Has(flag::Swimming), 0.4f);
        ease(v.lieAmount, e.Has(flag::LayingDown), 0.4f);
        v.glideTime = e.Has(flag::Gliding) ? v.glideTime + dt : 0.0f;
        ease(v.swellAmount, e.Has(flag::Ignited), 1.5f);
    }

    void EntityRenderer::RunAnimations(const Entity& e, View& v, const ClientEntityDef& def, Pose& pose, const FrameIn& in,
                                       const attachable::Geometry* bindGeometry) {
        (void)in;
        (void)bindGeometry;
        molang::Scope& scope = v.scope;
        const double now = v.now;
        const uint32_t epoch = ++v.animEpoch;
        const auto activate = [&](const std::string& key) {
            auto it = v.animStart.find(key);
            if (it == v.animStart.end())
                it = v.animStart.emplace(key, View::AnimStamp{now, epoch}).first;
            it->second.seen = epoch;
            return it->second.start;
        };
        EntityHost* host = static_cast<EntityHost*>(scope.host);

        std::function<void(const std::string&, float, int, const std::string&)> run;
        std::function<void(const std::string&, float, int, const std::string&)> runId;
        run = [&](const std::string& shortName, float weight, int depth, const std::string& path) {
            if (weight == 0.0f || depth > 4)
                return;
            const auto found = def.animations.find(shortName);
            if (found == def.animations.end())
                return;
            runId(found->second, weight, depth, path);
        };
        runId = [&](const std::string& id, float weight, int depth, const std::string& path) {
            if (weight == 0.0f || depth > 4)
                return;
            if (const auto* controller = assets.animations.FindController(id)) {
                auto& cs = v.ctrl[path + id];
                if (!cs.started) {
                    cs.started = true;
                    cs.state = controller->initial;
                    cs.entered = now;
                }
                std::string key;
                for (int guard = 0; guard < 4; guard++) {
                    const auto st = controller->states.find(cs.state);
                    if (st == controller->states.end())
                        break;
                    host->animTime = static_cast<float>(now - cs.entered);
                    {
                        bool all = true, any = false;
                        for (const auto& [animShort, blend] : st->second.animations) {
                            (void)blend;
                            const auto child = def.animations.find(animShort);
                            const Animation* ca = child != def.animations.end() ? assets.animations.Find(child->second) : nullptr;
                            if (!ca) {
                                all = false;
                                continue;
                            }
                            key.assign(path).append(id).append("/").append(cs.state).append("/").append(child->second);
                            const auto startIt = v.animStart.find(key);
                            const double start = startIt != v.animStart.end() ? startIt->second.start : cs.entered;
                            const bool done = ca->loop != Animation::Loop::Yes && ca->length > 0 && now - start >= ca->length;
                            all = all && done;
                            any = any || done;
                        }
                        host->allFinished = all && !st->second.animations.empty();
                        host->anyFinished = any;
                    }
                    bool moved = false;
                    for (const auto& [target, test] : st->second.transitions) {
                        if (!test || !test->Eval(scope).Truthy())
                            continue;
                        for (const auto& s : st->second.onExit)
                            s->Eval(scope);
                        cs.state = target;
                        cs.entered = now;
                        if (const auto next = controller->states.find(target); next != controller->states.end())
                            for (const auto& s : next->second.onEntry)
                                s->Eval(scope);
                        moved = true;
                        break;
                    }
                    if (!moved)
                        break;
                }
                const auto st = controller->states.find(cs.state);
                if (st == controller->states.end())
                    return;
                if (PERLIN_ENV("PERLIN_POSE_DEBUG"))
                    std::cout << "[ctrl] " << id << " state " << cs.state << std::endl;
                const std::string childPath = path + id + "/" + cs.state + "/";
                for (const auto& [animShort, blend] : st->second.animations) {
                    const float w = blend ? static_cast<float>(blend->Number(scope)) : 1.0f;
                    run(animShort, w * weight, depth + 1, childPath);
                }
                return;
            }
            const Animation* a = assets.animations.Find(id);
            if (!a)
                return;
            const double start = activate(path + id);
            double since = now - start;
            host->animTime = static_cast<float>(since);
            if (a->animTimeUpdate)
                since = a->animTimeUpdate->Number(scope);
            if (a->length > 0) {
                if (a->loop == Animation::Loop::Yes)
                    since = std::fmod(since, static_cast<double>(a->length));
                else
                    since = std::min(since, static_cast<double>(a->length));
            }
            host->animTime = static_cast<float>(since);
            if (PERLIN_ENV("PERLIN_POSE_DEBUG"))
                std::cout << "[anim] " << e.type << " " << id << " w=" << weight << std::endl;
            attachable::Apply(*a, scope, static_cast<float>(since), weight, pose);
        };
        for (const auto& entry : def.animate) {
            const float w = entry.weight ? static_cast<float>(entry.weight->Number(scope)) : 1.0f;
            if (entry.name.rfind("ctrl:", 0) == 0) {
                if (const auto c = def.controllers.find(entry.name.substr(5)); c != def.controllers.end())
                    runId(c->second, w, 0, "");
            } else {
                run(entry.name, w, 0, "");
            }
        }
        for (auto it = v.animStart.begin(); it != v.animStart.end();)
            it = it->second.seen == epoch ? std::next(it) : v.animStart.erase(it);
        (void)e;
    }

    bool EntityRenderer::Build(const Entity& e, View& v, const FrameIn& in, std::vector<Layer>& layers, float& outScale) {
        struct FpPoseGuard { FpPoseGuard(bool on) { attachable::gFirstPersonPose = on; } ~FpPoseGuard() { attachable::gFirstPersonPose = false; } } fpPoseGuard(v.fp);
        const ClientEntityDef* def = assets.Entity(e.type);
        if (!def)
            return false;
        EntityHost host;
        host.e = &e;
        host.dt = v.dt;
        host.now = in.time;
        host.limbSwing = v.limbSwing;
        host.limbAmount = v.limbAmount;
        host.groundSpeed = v.groundSpeed;
        host.verticalSpeed = v.verticalSpeed;
        host.capeFlap = v.capeFlap;
        host.bodyYaw = v.yaw;
        host.headYaw = v.headYaw;
        host.pitch = v.pitch;
        host.life = static_cast<float>(in.time - v.born);
        host.dx = static_cast<float>(v.pos.x - v.lastPos.x) * 20.0f;
        host.dy = static_cast<float>(v.pos.y - v.lastPos.y) * 20.0f;
        host.dz = static_cast<float>(v.pos.z - v.lastPos.z) * 20.0f;
        host.yawSpeed = v.yawSpeed;
        host.standAmount = v.standAmount;
        host.sitAmount = v.sitAmount;
        host.swimAmount = v.swimAmount;
        host.lieAmount = v.lieAmount;
        host.swellAmount = v.swellAmount;
        host.hurtTime = v.hurtTimer * 20.0f;
        host.deathTicks = v.deathTime * 20.0f;
        host.pos = v.pos;
        host.camera = in.camera;
        host.cameraYaw = in.cameraYaw;
        host.cameraPitch = in.cameraPitch;
        host.firstPerson = v.fp;
        {
            const std::string& usedItem = e.mainHand;
            float maxUse = 72000.0f;
            if (usedItem == "minecraft:crossbow") maxUse = 25.0f;
            else if (HandRenderer::IsFood(usedItem)) maxUse = 32.0f;
            else if (usedItem == "minecraft:spyglass") maxUse = 1200.0f;
            host.maxUseTicks = maxUse;
            if (v.fp && fp) {
                host.using_ = fp->using_;
                host.usedTicks = fp->usedTicks;
                host.blocking = fp->blocking;
                host.offArmHeight = fp->offArmHeight;
            } else {
                host.using_ = e.Has(flag::UsingItem);
                host.usedTicks = host.using_ ? static_cast<float>((v.now - v.usingSince) * 20.0) : 0.0f;
                host.blocking = host.using_ && (usedItem == "minecraft:shield" || e.offHand == "minecraft:shield");
            }
        }
        host.timeOfDay = std::fmod(in.worldTicks, 24000.0) / 24000.0;
        host.day = std::floor(in.worldTicks / 24000.0);
        v.scope.host = &host;
        v.scope.queryCache.clear();
        v.scope.cacheQueries = true;
        struct CacheGuard { molang::Scope& sc; ~CacheGuard() { sc.cacheQueries = false; sc.queryCache.clear(); } } cacheGuard{v.scope};
        v.hasArmR = v.hasArmL = false;
        auto& vars = v.scope.variables;
        vars["attack_time"] = Value::Num(v.attackTime);
        vars["gliding_speed_value"] = Value::Num(1.0);
        vars["is_first_person"] = Value::Num(v.fp ? 1 : 0);
        if (v.fp && fp) {
            vars["attack_time"] = Value::Num(fp->attackTime);
            vars["player_arm_height"] = Value::Num(fp->armHeight);
            vars["bob_animation"] = Value::Num(fp->bob ? 1 : 0);
            vars["short_arm_offset_right"] = Value::Num(0);
            vars["short_arm_offset_left"] = Value::Num(0);
            vars["is_using_vr"] = Value::Num(0);
            vars["is_vertical_splitscreen"] = Value::Num(0);
            vars["is_horizontal_splitscreen"] = Value::Num(0);
        }
        vars["map_face_icon"] = Value::Num(0);
        vars["player_x_rotation"] = Value::Num(v.pitch);
        vars["is_paperdoll"] = Value::Num(0);
        vars["is_brandishing_spear"] = Value::Num(0);
        if (!v.initialised) {
            v.initialised = true;
            for (const auto& s : def->initialize)
                s->Eval(v.scope);
        }
        for (const auto& s : def->preAnimation)
            s->Eval(v.scope);
        if (e.isPlayer) {
            vars["swim_amount"] = Value::Num(v.swimAmount);
            vars["is_holding_right"] = Value::Num(e.mainHand.empty() ? 0.0 : 1.0);
            vars["is_holding_left"] = Value::Num(e.offHand.empty() ? 0.0 : 1.0);
            vars["is_holding_spyglass"] = Value::Num(e.Has(flag::UsingItem) && e.mainHand == "minecraft:spyglass" ? 1.0 : 0.0);
            vars["is_tooting_goat_horn"] = Value::Num(e.Has(flag::UsingItem) && e.mainHand == "minecraft:goat_horn" ? 1.0 : 0.0);
            vars["is_using_brush"] = Value::Num(e.Has(flag::UsingItem) && e.mainHand == "minecraft:brush" ? 1.0 : 0.0);
        }

        const attachable::Geometry* bindGeometry = nullptr;
        for (const auto& rcRef : def->renderControllers) {
            if (rcRef.condition && !rcRef.condition->Eval(v.scope).Truthy())
                continue;
            const RenderControllerDef* rc = assets.RenderController(rcRef.id);
            if (!rc || !rc->geometry)
                continue;
            v.scope.arrays.clear();
            for (const auto& [name, values] : rc->arrays)
                v.scope.arrays[name] = values;
            const Value g = rc->geometry->Eval(v.scope);
            const auto id = def->geometries.find(AfterDot(g.isString ? g.s : "geometry.default"));
            if (id != def->geometries.end())
                bindGeometry = assets.geometries.Find(id->second);
            if (bindGeometry)
                break;
        }
        Pose pose;
        host.bonePivot = [bindGeometry](const std::string& bone, int axis, double& out) {
            if (!bindGeometry || axis < 0 || axis > 2)
                return false;
            std::string lower = bone;
            for (char& ch : lower)
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            for (const auto& b : bindGeometry->bones)
                if (b.name == lower) {
                    out = b.pivot[axis];
                    return true;
                }
            return false;
        };
        RunAnimations(e, v, *def, pose, in, bindGeometry);
        if (PERLIN_ENV("PERLIN_POSE_DEBUG"))
            for (const auto& [bn, xf] : pose)
                std::cout << "[pose] " << e.type << " " << bn << " rot " << xf.rotation.x << "," << xf.rotation.y << "," << xf.rotation.z
                          << " pos " << xf.position.x << "," << xf.position.y << "," << xf.position.z << std::endl;
        if (e.isPlayer) {
            auto& r = pose["cape"].rotation;
            r.x = -v.capeX;
            r.z = -v.capeZ;
            r.y = v.capeZ;
        }
        if (v.fp) {
            static const float px = PERLIN_ENV("PERLIN_FPX") ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_FPX"))) : 1.0f;
            for (auto& [boneName, xf] : pose)
                xf.position.x *= px;
        }

        float scale = def->scale ? static_cast<float>(def->scale->Number(v.scope)) : 1.0f;
        if (scale <= 0.0f)
            scale = 1.0f;
        if (e.scale > 0.0f && !e.isPlayer)
            scale *= e.scale;
        outScale = scale;

        const attachable::GeometryLibrary* library = &assets.geometries;
        std::string playerGeometry;
        if (e.isPlayer && e.skin) {
            const SkinData& s = *e.skin;
            if (!s.geometryJson.empty()) {
                auto& g = skinGeometry[s.serial];
                if (!g) {
                    g = std::make_unique<attachable::GeometryLibrary>();
                    g->Load(s.geometryJson);
                }
                if (g->Size() > 0) {
                    library = g.get();
                    playerGeometry = s.geometryName;
                }
            }
            if (playerGeometry.empty() && s.slim)
                playerGeometry = "geometry.humanoid.customSlim";
        }

        const auto addLayer = [&](const std::string& controllerId, bool allowCapeOnly) {
            (void)allowCapeOnly;
            const RenderControllerDef* rc = assets.RenderController(controllerId);
            if (!rc) {
                return;
            }
            v.scope.arrays.clear();
            for (const auto& [name, values] : rc->arrays)
                v.scope.arrays[name] = values;
            std::string geometryRef = "geometry.default", textureRef = "texture.default";
            if (rc->geometry) {
                const Value g = rc->geometry->Eval(v.scope);
                if (g.isString)
                    geometryRef = g.s;
            }
            if (!rc->textures.empty()) {
                const Value t = rc->textures.front()->Eval(v.scope);
                if (t.isString)
                    textureRef = t.s;
            }
            const std::string geoKey = AfterDot(geometryRef), texKey = AfterDot(textureRef);
            const auto geoId = def->geometries.find(geoKey);
            if (geoId == def->geometries.end())
                return;
            std::string geometryId = geoId->second;
            const attachable::GeometryLibrary* lib = &assets.geometries;
            if (e.isPlayer && geoKey == "default" && !playerGeometry.empty()) {
                geometryId = playerGeometry;
                lib = library;
            }
            const attachable::Geometry* geometry = lib->Find(geometryId);
            if (!geometry && lib != &assets.geometries)
                geometry = assets.geometries.Find(def->geometries.at(geoKey));
            if (!geometry)
                geometry = assets.geometries.Find(geoId->second);
            if (!geometry) {
                if (!warned[geometryId]) {
                    warned[geometryId] = true;
                    std::cout << "[entity] geometry " << geometryId << " not found for " << e.type << std::endl;
                }
                return;
            }
            if (!allowCapeOnly && !v.hasArmR && !v.hasArmL) {
                const glm::mat4 toJava = glm::scale(glm::mat4(1.0f), glm::vec3(16.0f, -16.0f, 16.0f));
                glm::mat4 w;
                glm::vec3 pv;
                if (attachable::BoneWorld(*geometry, pose, "rightarm", w, pv)) {
                    v.armR = w * glm::translate(glm::mat4(1.0f), pv) * toJava;
                    v.hasArmR = true;
                }
                if (attachable::BoneWorld(*geometry, pose, "leftarm", w, pv)) {
                    v.armL = w * glm::translate(glm::mat4(1.0f), pv) * toJava;
                    v.hasArmL = true;
                }
                v.hasItemR = attachable::BoneWorld(*geometry, pose, "rightitem", v.itemR, pv);
                if (v.hasItemR)
                    v.itemR = v.itemR * glm::translate(glm::mat4(1.0f), pv);
                v.hasItemL = attachable::BoneWorld(*geometry, pose, "leftitem", v.itemL, pv);
                if (v.hasItemL)
                    v.itemL = v.itemL * glm::translate(glm::mat4(1.0f), pv);
            }
            Layer layer;
            if (e.isPlayer && e.skin && (texKey == "default" || texKey == "cape")) {
                if (texKey == "cape" && e.skin->capeWidth <= 0)
                    return;
                layer.texture = texKey == "cape" ? "#cape" : "#skin";
            } else {
                const auto tp = def->textures.find(texKey);
                if (tp == def->textures.end())
                    return;
                layer.texture = tp->second;
            }
            std::vector<std::pair<std::string, bool>> explicitVisibility;
            bool defaultVisible = true;
            std::vector<std::pair<std::string, bool>> rules;
            for (const auto& part : rc->parts)
                rules.emplace_back(part.pattern, part.visible ? part.visible->Eval(v.scope).Truthy() : true);
            (void)explicitVisibility;
            (void)defaultVisible;
            const attachable::BoneFilter filter = rules.empty() ? attachable::BoneFilter() : attachable::BoneFilter([&](const std::string& bone) {
                bool visible = true;
                for (const auto& [pattern, on] : rules)
                    if (Matches(pattern, bone))
                        visible = on;
                return visible;
            });
            std::vector<std::pair<std::string, std::string>> materialRules;
            for (const auto& r : rc->materials) {
                if (!r.value)
                    continue;
                const Value mv = r.value->Eval(v.scope);
                materialRules.emplace_back(r.pattern, LowerCase(AfterDot(mv.isString ? mv.s : "default")));
            }
            const auto materialOf = [&](const std::string& bone) {
                std::string key = "default";
                for (const auto& [pattern, material] : materialRules)
                    if (Matches(pattern, bone))
                        key = material;
                return key;
            };
            std::vector<std::string> groups;
            if (materialRules.empty())
                groups.push_back("default");
            else
                for (const auto& bone : geometry->bones) {
                    const std::string key = materialOf(bone.name);
                    if (std::find(groups.begin(), groups.end(), key) == groups.end())
                        groups.push_back(key);
                }
            if (groups.size() > 6)
                groups.resize(6);
            if (rc->hasColor) {
                for (int i = 0; i < 4; i++) {
                    v.scope.thisValue = 1.0f;
                    layer.tint[i] = rc->color[i] ? static_cast<float>(rc->color[i]->Number(v.scope)) : 1.0f;
                }
            }
            layer.translucent = rc->translucent;
            for (const std::string& group : groups) {
                const bool single = groups.size() == 1;
                const attachable::BoneFilter groupFilter = single ? filter : attachable::BoneFilter([&](const std::string& bone) {
                    return materialOf(bone) == group && (!filter || filter(bone));
                });
                attachable::BuiltMesh mesh;
                attachable::BuildMesh(*geometry, pose, nullptr, mesh, groupFilter, nullptr, true);
                if (mesh.body.empty())
                    continue;
                static const bool debug = PERLIN_ENV("PERLIN_ENTITY_DEBUG") != nullptr;
                if (debug && !warned["dbg:" + e.type + controllerId + group]) {
                    warned["dbg:" + e.type + controllerId + group] = true;
                    std::cout << "[entity] " << e.type << " controller " << controllerId << " geometry " << geometryId << " texture "
                              << layer.texture << " material " << group << " quads " << mesh.body.size() << " (bones "
                              << geometry->bones.size() << ")" << std::endl;
                }
                Layer copy = layer;
                const auto named = def->materials.find(group);
                copy.material = assets.materialLibrary.Find(named != def->materials.end() ? named->second : group);
                copy.quads = std::move(mesh.body);
                layers.push_back(std::move(copy));
            }
        };

        for (const auto& rcRef : def->renderControllers) {
            if (rcRef.condition && !rcRef.condition->Eval(v.scope).Truthy())
                continue;
            addLayer(rcRef.id, false);
        }
        if (e.isPlayer && e.skin && e.skin->capeWidth > 0 && !e.Has(flag::Invisible) && !v.fp && e.armor[1] != "minecraft:elytra")
            addLayer("controller.render.player.cape", true);
        if (itemRenderer && !e.Has(flag::Invisible) && e.isPlayer) {
            const auto bind = [&](const std::string& name, const glm::mat4& boneWorld, bool left) {
                if (name.empty() || (v.fp && name != "minecraft:bow"))
                    return;
                static const bool bedrockAttach = PERLIN_ENV("PERLIN_BEDROCK_ATTACH") != nullptr;
                if (!v.fp && !bedrockAttach && (name == "minecraft:bow" || name == "minecraft:crossbow"))
                    return;
                attachable::Frame frame;
                if (!itemRenderer->ThirdPersonAttachable(name, left, v.fp, e.Has(flag::Sneaking), host.using_ && !left, host.usedTicks,
                                                         host.blocking, static_cast<double>(in.time - v.born), e.mainHand, e.offHand,
                                                         frame))
                    return;
                for (const attachable::Part& part : frame.parts) {
                    Layer layer;
                    layer.texture = part.texture;
                    static const glm::vec3 fpScale = [] {
                        glm::vec3 f(1.0f, 1.0f, 1.0f);
                        if (const char* e = PERLIN_ENV("PERLIN_FPF"))
                            std::sscanf(e, "%f,%f,%f", &f.x, &f.y, &f.z);
                        return f;
                    }();
                    const glm::mat4 fpFix = v.fp ? glm::scale(glm::mat4(1.0f), fpScale) : glm::mat4(1.0f);
                    const glm::mat4 bone = (part.texture.rfind("textures/entity/", 0) == 0
                                               ? boneWorld * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, kAttachDrop, 0.0f))
                                               : boneWorld) * fpFix;
                    layer.material = assets.materialLibrary.Find("entity_alphatest");
                    layer.quads = part.quads;
                    for (attachable::Quad& q : layer.quads) {
                        for (glm::vec3& p : q.p)
                            p = glm::vec3(bone * glm::vec4(p, 1.0f));
                        q.n = glm::normalize(glm::mat3(bone) * q.n);
                    }
                    layers.push_back(std::move(layer));
                }
                v.attachedItem[left ? 1 : 0] = true;
            };
            v.attachedItem[0] = v.attachedItem[1] = false;
            if (v.hasItemR)
                bind(e.mainHand, v.itemR, false);
            if (v.hasItemL)
                bind(e.offHand, v.itemL, true);
        }
        if (itemRenderer && !e.Has(flag::Invisible) && !layers.empty() && !v.fp) {
            static const int kOrder[4] = {0, 1, 2, 3};
            for (int slot : kOrder) {
                const std::string& item = e.armor[slot];
                if (item.empty())
                    continue;
                std::string geometryId, texture;
                bool found = false;
                if (e.isPlayer)
                    found = itemRenderer->ArmorAttachable(item + ".player", geometryId, texture);
                if (!found)
                    found = itemRenderer->ArmorAttachable(item, geometryId, texture);
                if (PERLIN_ENV("PERLIN_ARMOR_DEBUG"))
                    std::cout << "[armor] " << item << " found=" << found << " geo=" << geometryId << " tex=" << texture << std::endl;
                if (!found)
                    continue;
                const attachable::Geometry* geometry = assets.geometries.Find(geometryId);
                if (!geometry && library)
                    geometry = library->Find(geometryId);
                if (!geometry) {
                    if (!warned["armor:" + geometryId]) {
                        warned["armor:" + geometryId] = true;
                        std::cout << "[entity] armour geometry " << geometryId << " not found" << std::endl;
                    }
                    continue;
                }
                attachable::Pose elytraPose;
                const attachable::Pose* meshPose = &pose;
                if (item == "minecraft:elytra") {
                    elytraPose = pose;
                    elytraPose["body"].scale *= 1.067f;
                    glm::vec3 pl{4.5f, 4.0f, -2.0f}, rl{15.0f, 0.0f, -13.0f};
                    if (e.Has(flag::Gliding)) {
                        float ratio = 1.0f;
                        const float vy = v.verticalSpeed;
                        const float len = std::sqrt(v.groundSpeed * v.groundSpeed + vy * vy);
                        if (vy < 0.0f && len > 1e-4f)
                            ratio = 1.0f - std::pow(std::min(1.0f, -vy / len), 1.5f);
                        pl = {2.0f, 2.0f, -2.0f};
                        rl = {15.0f + 5.0f * ratio, 0.0f, -15.0f - 75.0f * ratio};
                    } else if (e.Has(flag::Swimming)) {
                        pl = {5.0f, 2.0f, -2.0f};
                        rl = {v.swimAmount * 15.0f, 0.0f, -15.0f};
                    } else if (e.Has(flag::Sneaking)) {
                        pl = {2.0f, 3.0f, -2.0f};
                        rl = {20.0f, -15.0f, -45.0f};
                    }
                    const glm::vec3 pr{-pl.x, pl.y, pl.z};
                    const glm::vec3 rr{rl.x, -rl.y, -rl.z};
                    elytraPose["left_wing"] = {pl, rl, glm::vec3(1.0f, 1.0f, 2.0f)};
                    elytraPose["right_wing"] = {pr, rr, glm::vec3(1.0f, 1.0f, 2.0f)};
                    meshPose = &elytraPose;
                }
                attachable::BuiltMesh mesh;
                glm::mat4 rootWorld(1.0f);
                glm::vec3 rootPivot(0.0f);
                bool haveRoot = false;
                if (bindGeometry && e.isPlayer)
                    haveRoot = attachable::BoneWorld(*bindGeometry, pose, "root", rootWorld, rootPivot);
                attachable::BuildMesh(*geometry, *meshPose, nullptr, mesh, attachable::BoneFilter(), haveRoot ? &rootWorld : nullptr, true);
                if (PERLIN_ENV("PERLIN_ARMOR_DEBUG"))
                {
                    glm::vec3 lo(1e9f), hi(-1e9f);
                    for (const auto& q : mesh.body)
                        for (const auto& pt : q.p) { lo = glm::min(lo, pt); hi = glm::max(hi, pt); }
                    std::cout << "[armor]   quads=" << mesh.body.size() << " bones=" << geometry->bones.size() << " lo=" << lo.x << "," << lo.y << "," << lo.z << " hi=" << hi.x << "," << hi.y << "," << hi.z << std::endl;
                }
                if (mesh.body.empty())
                    continue;
                Layer layer;
                layer.texture = (item == "minecraft:elytra" && e.isPlayer && e.skin && e.skin->capeWidth > 0) ? std::string("#cape") : texture;
                layer.material = assets.materialLibrary.Find("entity_alphatest");
                layer.quads = std::move(mesh.body);
                layers.push_back(std::move(layer));
            }
        }
        v.scope.host = nullptr;
        return !layers.empty();
    }

    namespace { double gEntityProf[3] = {}; int gEntityProfFrames = 0; }

    void EntityRenderer::Draw(const Entity& e, const View& v, const std::vector<Layer>& layers, float scale, const FrameIn& in) {
        static const glm::vec3 l0 = glm::normalize(glm::vec3(0.2f, 1.0f, -0.7f));
        static const glm::vec3 l1 = glm::normalize(glm::vec3(-0.2f, 1.0f, 0.7f));
        bool billboard = false;
        if (const ClientEntityDef* bd = assets.Entity(e.type))
            for (const auto& [shortName, id] : bd->animations)
                if (id == "animation.actor.billboard") {
                    billboard = true;
                    break;
                }
        const bool mirrored = !fpModel && !billboard;
        const float rotation = (fpModel || billboard) ? 180.0f : -v.yaw;
        const glm::mat4 mirrorZ = mirrored ? glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, -1.0f)) : glm::mat4(1.0f);
        const glm::mat3 rot = glm::mat3(glm::rotate(glm::mat4(1.0f), rotation * kDeg, glm::vec3(0, 1, 0)) * mirrorZ);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(v.pos));
        model = glm::rotate(model, rotation * kDeg, glm::vec3(0, 1, 0)) * mirrorZ;
        if (v.deathTime > 0.0f) {
            const float t = v.deathTime * 20.0f;
            const float f = std::min(std::sqrt(std::max(0.0f, (t - 1.0f) / 20.0f * 1.6f)), 1.0f);
            model = glm::translate(model, glm::vec3(0, 0.0f, 0));
            model = glm::rotate(model, f * 90.0f * kDeg, glm::vec3(0, 0, 1));
        }
        if (v.glideTime > 0.0f && e.isPlayer) {
            const float t = std::min(v.glideTime * 20.0f, 10.0f);
            const float amount = std::clamp(t * t / 100.0f, 0.0f, 1.0f);
            model = glm::rotate(model, -amount * (90.0f + v.pitch) * kDeg, glm::vec3(1, 0, 0));
        }
        if (e.type == "minecraft:arrow")
            model = glm::rotate(model, 180.0f * kDeg, glm::vec3(0, 1, 0));
        if (e.type == "minecraft:thrown_trident") {
            model = glm::rotate(model, (90.0f + v.pitch) * kDeg, glm::vec3(1, 0, 0));
            model = glm::translate(model, glm::vec3(0.0f, -12.0f / 16.0f * scale, 0.0f));
        }
        model = glm::scale(model, glm::vec3(scale / 16.0f));
        glm::vec3 light(1.0f);
        if (in.light)
            light = in.light(v.pos + glm::dvec3(0.0, 1.0, 0.0));
        if (fpModel) {
            model = *fpModel;
            if (fp)
                light = fp->light;
        }
        if (dollModel) {
            model = *dollModel * glm::rotate(glm::mat4(1.0f), rotation * kDeg, glm::vec3(0, 1, 0)) * mirrorZ * glm::scale(glm::mat4(1.0f), glm::vec3(scale / 16.0f));
            light = glm::vec3(1.0f);
        }
        const float hurt = v.hurtTimer > 0.0f ? std::min(1.0f, v.hurtTimer / 0.5f) : 0.0f;

        std::vector<const Layer*> ordered;
        for (const Layer& l : layers)
            ordered.push_back(&l);
        std::stable_sort(ordered.begin(), ordered.end(), [](const Layer* a, const Layer* b) {
            return static_cast<int>(a->material.blend) < static_cast<int>(b->material.blend);
        });
        for (const Layer* layerPtr : ordered) {
            const Layer& layer = *layerPtr;
            uint32_t texture = 0;
            if (layer.texture == "#skin")
                texture = TextureFor(e, "default");
            else if (layer.texture == "#cape")
                texture = TextureFor(e, "cape");
            else {
                auto it = textures.find(layer.texture);
                if (it == textures.end()) {
                    Texture t;
                    uint32_t id = 0;
                    if (ui::UiAssets::Instance().Image(layer.texture, t) && t.width > 0)
                        id = renderer->UiCreateTexture(t.width, t.height, t.pixels.data(), false);
                    else if (!warned[layer.texture]) {
                        warned[layer.texture] = true;
                        std::cout << "[entity] texture " << layer.texture << " not found" << std::endl;
                    }
                    it = textures.emplace(layer.texture, id).first;
                }
                texture = it->second;
            }
            if (PERLIN_ENV("PERLIN_ENTITY_DEBUG") && !warned["draw:" + e.type + layer.texture]) {
                warned["draw:" + e.type + layer.texture] = true;
                std::cout << "[entity]   draw " << e.type << " tex " << layer.texture << " id " << texture << " scale " << scale << " pos "
                          << v.pos.x << "," << v.pos.y << "," << v.pos.z << " quads " << layer.quads.size() << std::endl;
            }
            if (!texture)
                continue;
            std::vector<UiVertex> vertices;
            std::vector<uint32_t> indices;
            vertices.reserve(layer.quads.size() * 4);
            indices.reserve(layer.quads.size() * 6);
            const float layerHurt = layer.material.hurtTint ? hurt : 0.0f;
            const bool fullbright = layer.material.emissive == 2;
            const glm::vec3 tint(layer.tint[0], layer.tint[1], layer.tint[2]);
            for (const attachable::Quad& q : layer.quads) {
                const glm::vec3 n = rot * q.n;
                const float lit = std::min(1.0f, (std::max(0.0f, glm::dot(n, l0)) + std::max(0.0f, glm::dot(n, l1))) * 0.6f + 0.4f);
                const uint32_t color = fullbright ? PackColor(tint, layer.tint[3]) : PackColor(light * lit * tint, layer.tint[3]);
                const uint32_t base = static_cast<uint32_t>(vertices.size());
                for (int i = 0; i < 4; i++)
                    vertices.push_back({q.p[i].x, q.p[i].y, q.p[i].z, color, q.uv[i].x, q.uv[i].y});
                for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                    indices.push_back(base + i);
            }
            const uint32_t mesh = renderer->UiCreateFrameMesh(vertices.data(), static_cast<uint32_t>(vertices.size()), indices.data(),
                                                         static_cast<uint32_t>(indices.size()));
            if (PERLIN_ENV("PERLIN_ENTITY_DEBUG") && !warned["mesh:" + e.type]) {
                warned["mesh:" + e.type] = true;
                std::cout << "[entity]   mesh " << mesh << " verts " << vertices.size() << " v0 " << vertices[0].x << "," << vertices[0].y << ","
                          << vertices[0].z << " uv " << vertices[0].u << "," << vertices[0].v << " color " << std::hex << vertices[0].abgr
                          << std::dec << std::endl;
            }
            if (!mesh)
                continue;
            pendingMeshes.push_back(mesh);
            UiDraw draw;
            draw.mesh = mesh;
            draw.texture = texture;
            std::copy_n(&model[0][0], 16, draw.model);
            draw.premultiplied = false;
            using Blend = MaterialInfo::Blend;
            draw.alphaCutoff = layer.material.blend == Blend::AlphaTest ? 0.1f
                               : layer.material.blend == Blend::Opaque && layer.material.emissive == 0 ? 0.0f
                               : layer.material.blend == Blend::Opaque ? 0.0f : 0.01f;
            if (layer.material.blend == Blend::AlphaTest || layer.material.blend == Blend::Opaque)
                draw.blend = 0;
            else
                draw.blend = layer.material.blend == Blend::Additive ? 2 : 1;
            draw.emissive = layer.material.emissive;
            if (layerHurt > 0.0f) {
                draw.overlay[0] = 1.0f; draw.overlay[1] = 0.0f; draw.overlay[2] = 0.0f; draw.overlay[3] = 0.3f;
            }
            draw.depthWrite = layer.material.depthWrite && draw.blend != 2;
            draw.depth = true;
            if (fpModel && PERLIN_ENV("PERLIN_FP_DEBUG"))
                std::cout << "[fp] submit mesh=" << mesh << " tex=" << texture << " blend=" << draw.blend << " cutoff=" << draw.alphaCutoff << " verts=" << vertices.size() << " c=" << std::hex << vertices[0].abgr << std::dec << std::endl;
            if (dollModel && dollClip) {
                draw.scissor = true;
                std::copy_n(dollClip, 4, draw.scissorRect);
            }
            renderer->UiSubmit(dollModel ? UiLayer::Doll : fpModel ? UiLayer::Hand : UiLayer::World, draw);
        }
        if (itemRenderer && !e.Has(flag::Invisible) && !fpModel && !dollModel) {
            const auto held = [&](const std::string& name, const glm::mat4& arm, bool left) {
                if (name.empty())
                    return;
                ui::HudItem item;
                item.name = name;
                item.count = 1;
                item.maxDamage = ui::MaxDurability(name);
                itemRenderer->DrawHeldItem(item, model * arm, left, light);
            };
            if (v.hasArmR && !v.attachedItem[0])
                held(e.mainHand, v.armR, false);
            if (v.hasArmL && !v.attachedItem[1])
                held(e.offHand, v.armL, true);
        }
    }

    void EntityRenderer::RenderFirstPerson(const Entity& e, const FrameIn& in, const FpState& state) {
        if (!renderer)
            return;
        const std::string& root = ui::UiAssets::Instance().VanillaRoot();
        if (root.empty())
            return;
        assets.Configure(root);
        auto& slot = views[e.runtimeId];
        if (!slot)
            slot = std::make_unique<View>();
        View& v = *slot;
        v.lastSeen = in.time;
        v.fp = true;
        fp = &state;
        Track(e, v, in);
        std::vector<Layer> layers;
        float scale = 1.0f;
        const bool built = Build(e, v, in, layers, scale);
        if (PERLIN_ENV("PERLIN_FP_DEBUG"))
            std::cout << "[fp] built=" << built << " layers=" << layers.size() << " main='" << e.mainHand << "'" << std::endl;
        if (built) {
            const float eyeBlocks = e.Has(flag::Sneaking) ? 1.27f : 1.62f;
            static const float eyeOverride = PERLIN_ENV("PERLIN_FPEYE") ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_FPEYE"))) : 20.0f;
            static const bool mirror = PERLIN_ENV("PERLIN_FPNOMIR") == nullptr;
            const float eyePx = eyeOverride > 0 ? eyeOverride : eyeBlocks * 16.0f / scale;
            const glm::mat4 flip = mirror ? glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, -1.0f))
                                          : glm::rotate(glm::mat4(1.0f), 3.14159265f, glm::vec3(0, 1, 0));
            glm::mat4 m = state.viewEffect * flip *
                          glm::scale(glm::mat4(1.0f), glm::vec3(scale / 16.0f)) * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -eyePx, 0.0f));
            if (PERLIN_ENV("PERLIN_FP_DEBUG"))
                for (const Layer& l : layers) {
                    glm::vec3 lo(1e9f), hi(-1e9f), mlo(1e9f), mhi(-1e9f);
                    for (const auto& q : l.quads)
                        for (const auto& pt : q.p) {
                            lo = glm::min(lo, pt); hi = glm::max(hi, pt);
                            const glm::vec3 w = glm::vec3(m * glm::vec4(pt, 1.0f));
                            mlo = glm::min(mlo, w); mhi = glm::max(mhi, w);
                        }
                    std::cout << "[fp] layer " << l.texture << " quads=" << l.quads.size() << " model " << lo.x << "," << lo.y << "," << lo.z << " .. " << hi.x << "," << hi.y << "," << hi.z
                              << " view " << mlo.x << "," << mlo.y << "," << mlo.z << " .. " << mhi.x << "," << mhi.y << "," << mhi.z << std::endl;
                }
            fpModel = &m;
            Draw(e, v, layers, scale, in);
            fpModel = nullptr;
        }
        v.fp = false;
        fp = nullptr;
    }

    void EntityRenderer::RenderDoll(const Entity& e, const FrameIn& in, float cx, float feetY, float pxPerBlock, const int clip[4]) {
        if (!renderer)
            return;
        const std::string& root = ui::UiAssets::Instance().VanillaRoot();
        if (root.empty())
            return;
        assets.Configure(root);
        auto& slot = views[e.runtimeId];
        if (!slot)
            slot = std::make_unique<View>();
        View& v = *slot;
        v.lastSeen = in.time;
        Track(e, v, in);
        std::vector<Layer> layers;
        float scale = 1.0f;
        if (Build(e, v, in, layers, scale)) {
            glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(cx, feetY, 0.0f)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(pxPerBlock, -pxPerBlock, pxPerBlock));
            dollModel = &m;
            dollClip = clip;
            Draw(e, v, layers, scale, in);
            dollModel = nullptr;
            dollClip = nullptr;
        }
    }

    void EntityRenderer::DrawShadow(const glm::dvec3& pos, float radius, float strength, const FrameIn& in) {
        if (PERLIN_ENV("PERLIN_SHADOW_DEBUG"))
            std::cout << "[shadow-0] " << (in.groundY ? 1 : 0) << " r " << radius << std::endl;
        if (!in.groundY || radius <= 0.0f)
            return;
        const double ground = in.groundY(pos.x, pos.y + 0.5, pos.z);
        if (PERLIN_ENV("PERLIN_SHADOW_DEBUG"))
            std::cout << "[shadow-g] y " << pos.y << " ground " << ground << std::endl;
        if (ground < -500.0)
            return;
        const float height = static_cast<float>(pos.y - ground);
        if (PERLIN_ENV("PERLIN_SHADOW_DEBUG"))
            std::cout << "[shadow-in] y " << pos.y << " ground " << ground << " r " << radius << std::endl;
        if (height < -0.5f || height > 2.0f * radius * 2.0f + 8.0f)
            return;
        const double camDist2 = glm::dot(pos - in.camera, pos - in.camera);
        const float fade = static_cast<float>(1.0 - camDist2 / 256.0);
        if (fade <= 0.0f)
            return;
        float alpha = (strength * fade - std::max(0.0f, height) * 0.5f) * 0.5f;
        if (in.light) {
            const glm::vec3 l = in.light(pos + glm::dvec3(0.0, 0.5, 0.0));
            alpha *= std::clamp((l.x + l.y + l.z) / 3.0f, 0.0f, 1.0f);
        }
        alpha = std::clamp(alpha, 0.0f, 1.0f);
        if (PERLIN_ENV("PERLIN_SHADOW_DEBUG"))
            std::cout << "[shadow] pos " << pos.y << " ground " << ground << " alpha " << alpha << " r " << radius << std::endl;
        if (alpha <= 0.004f)
            return;
        if (!shadowTexture) {
            std::vector<uint8_t> px(32 * 32 * 4, 0);
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 32; x++) {
                    const float dx = (x + 0.5f - 16.0f) / 16.0f, dy = (y + 0.5f - 16.0f) / 16.0f;
                    const float d = std::sqrt(dx * dx + dy * dy);
                    const float a = std::clamp((0.94f - d) / 0.12f, 0.0f, 1.0f);
                    px[(static_cast<size_t>(y) * 32 + x) * 4 + 3] = static_cast<uint8_t>(a * 255.0f);
                }
            shadowTexture = renderer->UiCreateTexture(32, 32, px.data(), false);
        }
        if (!shadowTexture)
            return;
        const uint32_t color = PackColor(glm::vec3(0.0f), std::min(alpha, 1.0f));
        const float r = radius;
        const UiVertex v[4] = {{-r, 0, -r, color, 0, 0}, {r, 0, -r, color, 1, 0}, {r, 0, r, color, 1, 1}, {-r, 0, r, color, 0, 1}};
        const uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
        const uint32_t mesh = renderer->UiCreateFrameMesh(v, 4, idx, 6);
        if (!mesh)
            return;
        pendingMeshes.push_back(mesh);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(pos.x, ground + 0.015, pos.z));
        UiDraw draw;
        draw.mesh = mesh;
        draw.texture = shadowTexture;
        std::copy_n(&model[0][0], 16, draw.model);
        draw.premultiplied = false;
        draw.alphaCutoff = 0.004f;
        draw.blend = 1;
        draw.depthWrite = false;
        draw.depth = true;
        draw.emissive = 2;
        renderer->UiSubmit(UiLayer::World, draw);
    }

    void EntityRenderer::DrawParticles(const FrameIn& in) {
        if (!in.particles || in.particles->empty() || !renderer)
            return;
        if (!particleSheet && !particleSheetTried) {
            particleSheetTried = true;
            Texture t;
            if (ui::UiAssets::Instance().Image("textures/particle/particles", t) && t.width > 0)
                particleSheet = renderer->UiCreateTexture(t.width, t.height, t.pixels.data(), false);
        }
        const float yawR = in.cameraYaw * kDeg, pitchR = in.cameraPitch * kDeg;
        const glm::vec3 forward(-std::sin(yawR) * std::cos(pitchR), -std::sin(pitchR), std::cos(yawR) * std::cos(pitchR));
        const glm::vec3 right(-std::cos(yawR), 0.0f, -std::sin(yawR));
        const glm::vec3 up = glm::cross(right, forward);
        const glm::dvec3 base(std::floor(in.camera.x), std::floor(in.camera.y), std::floor(in.camera.z));
        struct Batch {
            std::vector<UiVertex> vertices;
            std::vector<uint32_t> indices;
        };
        std::unordered_map<int, Batch> batches;
        for (const particle::Quad& q : *in.particles) {
            Batch& b = batches[q.terrainTile];
            const glm::vec3 c = glm::vec3(q.centre - base);
            const glm::vec3 r = right * q.half, u = up * q.half;
            const uint32_t i0 = static_cast<uint32_t>(b.vertices.size());
            const glm::vec3 p0 = c - r - u, p1 = c + r - u, p2 = c + r + u, p3 = c - r + u;
            b.vertices.push_back({p0.x, p0.y, p0.z, q.abgr, q.u0, q.v1});
            b.vertices.push_back({p1.x, p1.y, p1.z, q.abgr, q.u1, q.v1});
            b.vertices.push_back({p2.x, p2.y, p2.z, q.abgr, q.u1, q.v0});
            b.vertices.push_back({p3.x, p3.y, p3.z, q.abgr, q.u0, q.v0});
            for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u, 0u, 2u, 1u, 0u, 3u, 2u})
                b.indices.push_back(i0 + k);
        }
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(base));
        for (auto& [tile, b] : batches) {
            uint32_t texture = 0;
            if (tile < 0) {
                texture = particleSheet;
            } else {
                auto it = particleTiles.find(tile);
                if (it == particleTiles.end()) {
                    uint32_t id = 0;
                    std::vector<uint8_t> rgba;
                    if (in.tilePixels && in.tilePixels(tile, rgba) && !rgba.empty()) {
                        const int side = static_cast<int>(std::lround(std::sqrt(static_cast<double>(rgba.size() / 4))));
                        id = renderer->UiCreateTexture(side, side, rgba.data(), false);
                    }
                    it = particleTiles.emplace(tile, id).first;
                }
                texture = it->second;
            }
            if (!texture || b.vertices.empty())
                continue;
            const uint32_t mesh = renderer->UiCreateFrameMesh(b.vertices.data(), static_cast<uint32_t>(b.vertices.size()), b.indices.data(),
                                                         static_cast<uint32_t>(b.indices.size()));
            if (!mesh)
                continue;
            pendingMeshes.push_back(mesh);
            UiDraw draw;
            draw.mesh = mesh;
            draw.texture = texture;
            std::copy_n(&model[0][0], 16, draw.model);
            draw.premultiplied = false;
            draw.blend = tile < 0 ? 1 : 0;
            draw.alphaCutoff = tile < 0 ? 0.02f : 0.5f;
            draw.depth = true;
            draw.depthWrite = tile >= 0;
            draw.emissive = 2;
            renderer->UiSubmit(UiLayer::World, draw);
        }
    }

    void EntityRenderer::DrawNametags(const FrameIn& in) {
        if (tags.empty() || !renderer)
            return;
        ui::McFont& font = ui::McFont::Instance();
        if (!fontTexture || fontTextureVersion != font.Version()) {
            if (fontTexture)
                renderer->UiDestroyTexture(fontTexture);
            fontTexture = renderer->UiCreateTexture(font.AtlasWidth(), font.AtlasHeight(), font.Atlas().data(), false);
            fontTextureVersion = font.Version();
        }
        if (!fontTexture)
            return;
        const float yawR = in.cameraYaw * kDeg, pitchR = in.cameraPitch * kDeg;
        const glm::vec3 forward(-std::sin(yawR) * std::cos(pitchR), -std::sin(pitchR), std::cos(yawR) * std::cos(pitchR));
        const glm::vec3 right(-std::cos(yawR), 0.0f, -std::sin(yawR));
        const glm::vec3 up = glm::cross(right, forward);
        constexpr float kScale = 0.025f;
        for (const Tag& tag : tags) {
            const float width = font.Width(tag.text);
            const float left = -width * 0.5f;
            std::vector<UiVertex> bgV, textV;
            std::vector<uint32_t> bgI, textI;
            const auto corner = [&](float x, float y) { return right * (x * kScale) + up * (-y * kScale); };
            const auto quad = [&](std::vector<UiVertex>& vv, std::vector<uint32_t>& ii, float x0, float y0, float x1, float y1, float u0,
                                  float v0, float u1, float v1, uint32_t color) {
                const uint32_t base = static_cast<uint32_t>(vv.size());
                const glm::vec3 p0 = corner(x0, y1), p1 = corner(x1, y1), p2 = corner(x1, y0), p3 = corner(x0, y0);
                vv.push_back({p0.x, p0.y, p0.z, color, u0, v1});
                vv.push_back({p1.x, p1.y, p1.z, color, u1, v1});
                vv.push_back({p2.x, p2.y, p2.z, color, u1, v0});
                vv.push_back({p3.x, p3.y, p3.z, color, u0, v0});
                for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u, 0u, 2u, 1u, 0u, 3u, 2u})
                    ii.push_back(base + i);
            };
            quad(bgV, bgI, left - 1.0f, -1.0f, left + width + 1.0f, 8.0f, 0, 0, 0, 0, PackColor(glm::vec3(0.0f), 0.25f));
            glm::vec3 col(1.0f);
            float pen = left;
            bool bold = false;
            for (size_t i = 0; i < tag.text.size();) {
                uint32_t cp = 0;
                if (!ui::McFont::Next(tag.text, i, cp))
                    break;
                if (cp == 0xA7 && i < tag.text.size()) {
                    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(tag.text[i++])));
                    static const char* kHex = "0123456789abcdef";
                    static const uint8_t kRgb[16][3] = {{0, 0, 0}, {0, 0, 170}, {0, 170, 0}, {0, 170, 170}, {170, 0, 0}, {170, 0, 170},
                                                        {255, 170, 0}, {170, 170, 170}, {85, 85, 85}, {85, 85, 255}, {85, 255, 85},
                                                        {85, 255, 255}, {255, 85, 85}, {255, 85, 255}, {255, 255, 85}, {255, 255, 255}};
                    if (const char* at = std::strchr(kHex, c); at && c != 0) {
                        const int idx = static_cast<int>(at - kHex);
                        col = glm::vec3(kRgb[idx][0], kRgb[idx][1], kRgb[idx][2]) / 255.0f;
                        bold = false;
                    } else if (c == 'l') {
                        bold = true;
                    } else if (c == 'r') {
                        col = glm::vec3(1.0f);
                        bold = false;
                    }
                    continue;
                }
                const ui::McFont::Glyph& g = font.Get(cp);
                if (g.width > 0)
                    quad(textV, textI, pen, 0.0f, pen + static_cast<float>(g.width), 8.0f, g.u0, g.v0, g.u1, g.v1, PackColor(col));
                pen += static_cast<float>(g.advance) + (bold ? 1.0f : 0.0f);
            }
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(tag.at.x, tag.at.y, tag.at.z));
            const auto submit = [&](std::vector<UiVertex> vv, const std::vector<uint32_t>& ii, uint32_t texture, bool seeThrough, float alpha) {
                if (vv.empty())
                    return;
                if (alpha != 1.0f)
                    for (UiVertex& v : vv) {
                        const uint32_t a = static_cast<uint32_t>(static_cast<float>(v.abgr >> 24) * alpha);
                        v.abgr = (v.abgr & 0x00FFFFFFu) | (a << 24);
                    }
                const uint32_t mesh = renderer->UiCreateFrameMesh(vv.data(), static_cast<uint32_t>(vv.size()), ii.data(), static_cast<uint32_t>(ii.size()));
                if (!mesh)
                    return;
                pendingMeshes.push_back(mesh);
                UiDraw draw;
                draw.mesh = mesh;
                draw.texture = texture;
                std::copy_n(&model[0][0], 16, draw.model);
                draw.premultiplied = false;
                draw.blend = 1;
                draw.alphaCutoff = 0.004f;
                draw.depth = !seeThrough;
                draw.depthWrite = false;
                draw.emissive = 2;
                renderer->UiSubmit(UiLayer::WorldLate, draw);
            };
            submit(bgV, bgI, 0, true, 1.0f);
            submit(textV, textI, fontTexture, true, tag.sneaking ? 1.0f : 32.0f / 255.0f);
            if (!tag.sneaking)
                submit(textV, textI, fontTexture, false, 1.0f);
        }
        tags.clear();
    }

    void EntityRenderer::DrawCracks(const FrameIn& in) {
        for (const auto& c : in.cracks) {
            const std::string path = "textures/environment/destroy_stage_" + std::to_string(std::clamp(c[3], 0, 9));
            auto it = textures.find(path);
            if (it == textures.end()) {
                Texture t;
                uint32_t id = 0;
                if (ui::UiAssets::Instance().Image(path, t) && t.width > 0)
                    id = renderer->UiCreateTexture(t.width, t.height, t.pixels.data(), false);
                it = textures.emplace(path, id).first;
            }
            if (!it->second)
                continue;
            const glm::dvec3 d = glm::dvec3(c[0] + 0.5, c[1] + 0.5, c[2] + 0.5) - in.camera;
            if (glm::dot(d, d) > 48.0 * 48.0)
                continue;
            const float lo = -0.002f, hi = 1.002f;
            const uint32_t white = 0xFF383838u;
            std::vector<UiVertex> v;
            std::vector<uint32_t> idx;
            const auto face = [&](glm::vec3 a, glm::vec3 b, glm::vec3 cc, glm::vec3 dd) {
                const uint32_t base = static_cast<uint32_t>(v.size());
                v.push_back({a.x, a.y, a.z, white, 0, 0});
                v.push_back({b.x, b.y, b.z, white, 1, 0});
                v.push_back({cc.x, cc.y, cc.z, white, 1, 1});
                v.push_back({dd.x, dd.y, dd.z, white, 0, 1});
                for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                    idx.push_back(base + i);
            };
            face({lo, hi, lo}, {hi, hi, lo}, {hi, hi, hi}, {lo, hi, hi});
            face({lo, lo, hi}, {hi, lo, hi}, {hi, lo, lo}, {lo, lo, lo});
            face({hi, hi, lo}, {lo, hi, lo}, {lo, lo, lo}, {hi, lo, lo});
            face({lo, hi, hi}, {hi, hi, hi}, {hi, lo, hi}, {lo, lo, hi});
            face({lo, hi, lo}, {lo, hi, hi}, {lo, lo, hi}, {lo, lo, lo});
            face({hi, hi, hi}, {hi, hi, lo}, {hi, lo, lo}, {hi, lo, hi});
            const uint32_t mesh = renderer->UiCreateFrameMesh(v.data(), static_cast<uint32_t>(v.size()), idx.data(), static_cast<uint32_t>(idx.size()));
            if (!mesh)
                continue;
            pendingMeshes.push_back(mesh);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(c[0], c[1], c[2]));
            UiDraw draw;
            draw.mesh = mesh;
            draw.texture = it->second;
            std::copy_n(&model[0][0], 16, draw.model);
            draw.premultiplied = false;
            draw.alphaCutoff = 0.004f;
            draw.blend = 1;
            draw.depthWrite = false;
            draw.depth = true;
            draw.emissive = 2;
            renderer->UiSubmit(UiLayer::World, draw);
        }
    }

    void EntityRenderer::Render(const std::vector<Entity>& entities, const FrameIn& in) {
        if (!renderer)
            return;
        for (uint32_t mesh : pendingMeshes)
            renderer->UiDestroyMesh(mesh);
        pendingMeshes.clear();
        const std::string& root = ui::UiAssets::Instance().VanillaRoot();
        if (root.empty())
            return;
        assets.Configure(root);
        drawn = considered = unrenderable = culled = 0;
        if (itemRenderer)
            itemRenderer->BeginWorldItems();
        tags.clear();
        for (const Entity& e : entities) {
            if (e.isSelf && in.firstPerson)
                continue;
            considered++;
            auto& slot = views[e.runtimeId];
            if (!slot)
                slot = std::make_unique<View>();
            View& v = *slot;
            v.lastSeen = in.time;
            Track(e, v, in);
            const glm::dvec3 delta = v.pos - in.camera;
            if (glm::dot(delta, delta) > static_cast<double>(in.maxDistance) * in.maxDistance)
                continue;
            if (e.Has(flag::Invisible) && !e.isPlayer)
                continue;
            if (e.dying && v.deathTime >= 1.0f && !e.isSelf)
                continue;
            if (in.frustum) {
                float w = 0.5f, h = 0.5f;
                if (!e.isItem) {
                    const auto hb = HitBox(e);
                    w = hb.first;
                    h = hb.second;
                }
                const float r = std::max(w, h) * 0.5f + 0.75f + (e.isItem ? 0.0f : 0.5f * h);
                const float x = static_cast<float>(v.pos.x), y = static_cast<float>(v.pos.y), z = static_cast<float>(v.pos.z);
                if (!in.frustum->Visible(x - r, y - 0.5f, z - r, x + r, y + std::max(h, 0.5f) + r, z + r)) {
                    culled++;
                    continue;
                }
            }
            if (!e.Has(flag::Invisible) && !(e.isSelf && in.firstPerson)) {
                if (e.isItem) {
                    DrawShadow(v.pos, 0.15f, 0.75f, in);
                } else if (!e.dying || v.deathTime < 0.9f) {
                    const auto hb = HitBox(e);
                    DrawShadow(v.pos, std::min(hb.first, 1.4f) * 0.8f * (e.scale > 0 ? e.scale : 1.0f), 1.0f, in);
                }
            }
            if (!e.isItem && !e.Has(flag::Invisible) && !(e.isSelf && (in.firstPerson || !in.ownNametag))) {
                const std::string& label = e.isPlayer ? e.name : e.nameTag;
                const double d2 = glm::dot(delta, delta);
                if (!label.empty() && d2 < (e.isPlayer ? 64.0 * 64.0 : 16.0 * 16.0)) {
                    const auto hb = HitBox(e);
                    Tag t;
                    t.text = label;
                    t.at = v.pos + glm::dvec3(0.0, static_cast<double>(hb.second * (e.scale > 0 ? e.scale : 1.0f)) + 0.5, 0.0);
                    t.sneaking = e.Has(flag::Sneaking);
                    tags.push_back(std::move(t));
                }
            }
            if (e.isItem) {
                if (itemRenderer) {
                    ui::HudItem item;
                    item.name = e.itemName;
                    item.count = e.itemCount;
                    item.aux = e.itemAux;
                    item.blockRuntimeId = e.itemBlock;
                    item.maxDamage = ui::MaxDurability(e.itemName);
                    item.enchanted = e.itemEnchanted;
                    glm::vec3 light(1.0f);
                    if (in.light)
                        light = in.light(v.pos + glm::dvec3(0.0, 0.5, 0.0));
                    itemRenderer->DrawWorldItem(item, v.pos, static_cast<float>((in.time - v.born) * 20.0), e.runtimeId, light);
                    drawn++;
                }
                continue;
            }
            if (e.type == "minecraft:xp_orb") {
                const double age = (in.time - v.born) * 20.0;
                View facing = v;
                facing.yaw = 180.0f;
                Layer layer;
                layer.texture = "textures/entity/experience_orb";
                const int icon = std::clamp(e.variant, 0, 10);
                const float u0 = static_cast<float>(icon % 4) * 0.25f, v0 = static_cast<float>(icon / 4) * 0.25f;
                const float yawR = in.cameraYaw * kDeg, pitchR = in.cameraPitch * kDeg;
                const glm::vec3 forward(-std::sin(yawR) * std::cos(pitchR), -std::sin(pitchR), std::cos(yawR) * std::cos(pitchR));
                const glm::vec3 right(-std::cos(yawR), 0.0f, -std::sin(yawR));
                const glm::vec3 up = glm::cross(right, forward);
                const auto corner = [&](float x, float y) {
                    return (right * x + up * y) * 0.3f * 16.0f + glm::vec3(0.0f, 0.1f * 16.0f, 0.0f);
                };
                attachable::Quad q;
                q.p[0] = corner(-0.5f, -0.25f); q.uv[0] = {u0, v0 + 0.25f};
                q.p[1] = corner(0.5f, -0.25f);  q.uv[1] = {u0 + 0.25f, v0 + 0.25f};
                q.p[2] = corner(0.5f, 0.75f);   q.uv[2] = {u0 + 0.25f, v0};
                q.p[3] = corner(-0.5f, 0.75f);  q.uv[3] = {u0, v0};
                q.n = forward * -1.0f;
                layer.quads.push_back(q);
                const float t = static_cast<float>(age) * 0.5f;
                layer.tint[0] = (std::sin(t) + 1.0f) * 0.5f;
                layer.tint[1] = 1.0f;
                layer.tint[2] = (std::sin(t + 4.1887903f) + 1.0f) * 0.1f;
                layer.tint[3] = 128.0f / 255.0f;
                layer.translucent = true;
                layer.material.blend = MaterialInfo::Blend::Alpha;
                layer.material.emissive = 2;
                layer.material.hurtTint = false;
                Draw(e, facing, {layer}, 1.0f, in);
                drawn++;
                continue;
            }
            const double dist2 = glm::dot(delta, delta);
            const int reuse = e.isPlayer || e.dying || e.Has(flag::Riding) ? 0 : dist2 > 80.0 * 80.0 ? 4 : dist2 > 48.0 * 48.0 ? 2 : dist2 > 24.0 * 24.0 ? 1 : 0;
            if (reuse > 0 && v.cacheAge < reuse && !v.cachedLayers.empty()) {
                v.cacheAge++;
                Draw(e, v, v.cachedLayers, v.cachedScale, in);
                drawn++;
                continue;
            }
            std::vector<Layer> layers;
            float scale = 1.0f;
            const auto prof0 = std::chrono::steady_clock::now();
            const bool built = Build(e, v, in, layers, scale);
            gEntityProf[0] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - prof0).count();
            if (built && reuse > 0) {
                v.cachedLayers = layers;
                v.cachedScale = scale;
                v.cacheAge = 0;
            }
            if (!built) {
                unrenderable++;
                if (!warned["build:" + e.type]) {
                    warned["build:" + e.type] = true;
                    std::cout << "[entity] cannot render " << e.type << (assets.Entity(e.type) ? " (no usable layer)" : " (no definition)") << std::endl;
                }
                continue;
            }
            const auto prof1 = std::chrono::steady_clock::now();
            Draw(e, v, layers, scale, in);
            gEntityProf[1] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - prof1).count();
            drawn++;
        }
        if (PERLIN_ENV("PERLIN_PROFILE") && ++gEntityProfFrames >= 120) {
            std::cout << "[profile] entity build=" << gEntityProf[0] / gEntityProfFrames << " draw=" << gEntityProf[1] / gEntityProfFrames << " shadow+rest=" << gEntityProf[2] / gEntityProfFrames << std::endl;
            gEntityProf[0] = gEntityProf[1] = gEntityProf[2] = 0;
            gEntityProfFrames = 0;
        }
        DrawParticles(in);
        DrawNametags(in);
        for (auto it = views.begin(); it != views.end();)
            it = in.time - it->second->lastSeen > 5.0 ? views.erase(it) : std::next(it);
        if (in.chunkBorders) {
            const int pcx = static_cast<int>(std::floor(in.playerPos.x)) >> 4, pcz = static_cast<int>(std::floor(in.playerPos.z)) >> 4;
            const int sectionY = static_cast<int>(std::floor(in.playerPos.y)) >> 4;
            const double minY = -64.0, maxY = 320.0;
            const double x0 = pcx * 16.0, z0 = pcz * 16.0, x1 = x0 + 16.0, z1 = z0 + 16.0;
            const float thin = 1.0f / 2.5f, thick = 4.0f / 2.5f;
            const auto argb = [](int r, int g, int b) { return 0xFF000000u | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(r); };
            const uint32_t red = 0x800000FFu;
            const uint32_t cell = argb(0, 155, 155);
            const uint32_t yellow = argb(255, 255, 0);
            const uint32_t major = argb(64, 64, 255);
            for (int j = -16; j <= 32; j += 16)
                for (int i = -16; i <= 32; i += 16)
                    lines.push_back({{x0 + i, minY, z0 + j}, {x0 + i, maxY, z0 + j}, red, thick});
            for (int k = 2; k < 16; k += 2) {
                const uint32_t c = k % 4 == 0 ? cell : yellow;
                lines.push_back({{x0 + k, minY, z0}, {x0 + k, maxY, z0}, c, thin});
                lines.push_back({{x0 + k, minY, z1}, {x0 + k, maxY, z1}, c, thin});
            }
            for (int k = 2; k < 16; k += 2) {
                const uint32_t c = k % 4 == 0 ? cell : yellow;
                lines.push_back({{x0, minY, z0 + k}, {x0, maxY, z0 + k}, c, thin});
                lines.push_back({{x1, minY, z0 + k}, {x1, maxY, z0 + k}, c, thin});
            }
            for (int y = static_cast<int>(minY); y <= static_cast<int>(maxY); y += 2) {
                const uint32_t c = y % 8 == 0 ? cell : yellow;
                lines.push_back({{x0, double(y), z0}, {x0, double(y), z1}, c, thin});
                lines.push_back({{x0, double(y), z1}, {x1, double(y), z1}, c, thin});
                lines.push_back({{x1, double(y), z1}, {x1, double(y), z0}, c, thin});
                lines.push_back({{x1, double(y), z0}, {x0, double(y), z0}, c, thin});
            }
            for (int j = 0; j <= 16; j += 16)
                for (int i = 0; i <= 16; i += 16)
                    lines.push_back({{x0 + i, minY, z0 + j}, {x0 + i, maxY, z0 + j}, major, thick});
            AddBox({x0, sectionY * 16.0, z0}, {x1, sectionY * 16.0 + 16.0, z1}, major, thin);
            for (int y = static_cast<int>(minY); y <= static_cast<int>(maxY); y += 16) {
                lines.push_back({{x0, double(y), z0}, {x0, double(y), z1}, major, thick});
                lines.push_back({{x0, double(y), z1}, {x1, double(y), z1}, major, thick});
                lines.push_back({{x1, double(y), z1}, {x1, double(y), z0}, major, thick});
                lines.push_back({{x1, double(y), z0}, {x0, double(y), z0}, major, thick});
            }
        }
        for (const Entity& hook : entities) {
            if (hook.type != "minecraft:fishing_hook" || hook.ownerUid == 0)
                continue;
            const Entity* owner = nullptr;
            for (const Entity& o : entities)
                if (o.uniqueId == hook.ownerUid && !o.isItem) {
                    owner = &o;
                    break;
                }
            const auto hv = views.find(hook.runtimeId);
            if (!owner || hv == views.end())
                continue;
            const auto ov = views.find(owner->runtimeId);
            const double ownerYaw = ov != views.end() ? ov->second->yaw : owner->yaw;
            const glm::dvec3 ownerPos = ov != views.end() ? ov->second->pos : owner->pos;
            glm::dvec3 hand;
            if (owner->isSelf && in.firstPerson) {
                const double yr = in.cameraYaw * kDeg, pr = in.cameraPitch * kDeg;
                const glm::dvec3 fwd(-std::sin(yr) * std::cos(pr), -std::sin(pr), std::cos(yr) * std::cos(pr));
                const glm::dvec3 rgt(-std::cos(yr), 0.0, -std::sin(yr));
                const glm::dvec3 upv = glm::cross(rgt, fwd);
                hand = in.camera + fwd * 0.65 + rgt * 0.38 - upv * 0.32;
            } else {
                const double f = ownerYaw * kDeg, s = std::sin(f), c = std::cos(f);
                hand = ownerPos + glm::dvec3(-c * 0.35 - s * 0.8, 1.62 * 0.8 + (owner->Has(flag::Sneaking) ? -0.1875 : 0.0), -s * 0.35 + c * 0.8);
            }
            const glm::dvec3 hookPos = hv->second->pos;
            const glm::dvec3 rel = hand - (hookPos + glm::dvec3(0.0, 0.25, 0.0));
            glm::dvec3 previous = hookPos + glm::dvec3(0.0, 0.25, 0.0);
            for (int i = 1; i <= 16; i++) {
                const double p = i / 16.0;
                const glm::dvec3 point = hookPos + glm::dvec3(rel.x * p, rel.y * (p * p + p) * 0.5 + 0.25, rel.z * p);
                lines.push_back({previous, point, 0xFF000000u, 0.4f});
                previous = point;
            }
        }
        DrawCracks(in);
        outlineLineStart = lines.size();
        if (in.outline) {
            if (!in.outlineShape.empty())
                AddShapeOutline(in.outlineShape, 0x66000000u);
            else
                AddBox(in.outlineLo, in.outlineHi, 0x66000000u);
        }
        DrawLines(in);
    }

    void EntityRenderer::AddBox(const glm::dvec3& lo, const glm::dvec3& hi, uint32_t colour, float width) {
        const glm::dvec3 c[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z},
                                 {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
        static const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& e : edges)
            lines.push_back({c[e[0]], c[e[1]], colour, width});
    }

    void EntityRenderer::AddShapeOutline(const std::vector<std::pair<glm::dvec3, glm::dvec3>>& boxes, uint32_t colour) {
        if (boxes.empty())
            return;
        std::vector<double> c[3];
        for (const auto& [lo, hi] : boxes)
            for (int a = 0; a < 3; a++) {
                c[a].push_back(lo[a]);
                c[a].push_back(hi[a]);
            }
        for (auto& v : c) {
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end(), [](double x, double y) { return std::fabs(x - y) < 1e-6; }), v.end());
        }
        const int n[3] = {static_cast<int>(c[0].size()) - 1, static_cast<int>(c[1].size()) - 1, static_cast<int>(c[2].size()) - 1};
        if (n[0] < 1 || n[1] < 1 || n[2] < 1)
            return;
        const auto filled = [&](int i, int j, int k) {
            if (i < 0 || j < 0 || k < 0 || i >= n[0] || j >= n[1] || k >= n[2])
                return false;
            const glm::dvec3 centre((c[0][i] + c[0][i + 1]) * 0.5, (c[1][j] + c[1][j + 1]) * 0.5, (c[2][k] + c[2][k + 1]) * 0.5);
            for (const auto& [lo, hi] : boxes)
                if (centre.x > lo.x && centre.x < hi.x && centre.y > lo.y && centre.y < hi.y && centre.z > lo.z && centre.z < hi.z)
                    return true;
            return false;
        };
        for (int axis = 0; axis < 3; axis++) {
            const int u = (axis + 1) % 3, w = (axis + 2) % 3;
            for (int pu = 0; pu <= n[u]; pu++)
                for (int pw = 0; pw <= n[w]; pw++) {
                    int runStart = -1;
                    for (int t = 0; t <= n[axis]; t++) {
                        bool edge = false;
                        if (t < n[axis]) {
                            bool f[4];
                            for (int du = 0; du < 2; du++)
                                for (int dw = 0; dw < 2; dw++) {
                                    int idx[3];
                                    idx[axis] = t;
                                    idx[u] = pu - 1 + du;
                                    idx[w] = pw - 1 + dw;
                                    f[du * 2 + dw] = filled(idx[0], idx[1], idx[2]);
                                }
                            const int count = f[0] + f[1] + f[2] + f[3];
                            edge = count == 1 || count == 3 || (count == 2 && f[0] == f[3]);
                        }
                        if (edge && runStart < 0)
                            runStart = t;
                        if (!edge && runStart >= 0) {
                            glm::dvec3 a, b;
                            a[axis] = c[axis][runStart];
                            b[axis] = c[axis][t];
                            a[u] = b[u] = c[u][pu];
                            a[w] = b[w] = c[w][pw];
                            lines.push_back({a, b, colour});
                            runStart = -1;
                        }
                    }
                }
        }
    }

    void EntityRenderer::DrawLines(const FrameIn& in) {
        if (lines.empty())
            return;
        std::vector<UiVertex> vertices;
        std::vector<uint32_t> indices;
        const double pixel = in.pixelScale > 0.0f ? in.pixelScale : 0.002;
        size_t lineIndex = 0;
        std::vector<Line> pieces;
        pieces.reserve(lines.size() * 2);
        const auto lerpColour = [](uint32_t c0, uint32_t c1, double t) {
            uint32_t out = 0;
            for (int sh = 0; sh < 32; sh += 8) {
                const double v0 = (c0 >> sh) & 0xFF, v1 = (c1 >> sh) & 0xFF;
                out |= static_cast<uint32_t>(std::lround(v0 + (v1 - v0) * t)) << sh;
            }
            return out;
        };
        size_t outlineFirstPiece = static_cast<size_t>(-1);
        for (size_t li = 0; li < lines.size(); li++) {
            const Line& l = lines[li];
            if (li == outlineLineStart)
                outlineFirstPiece = pieces.size();
            const double len = glm::length(l.b - l.a);
            const int n = len > 8.0 ? static_cast<int>(std::ceil(len / 8.0)) : 1;
            const uint32_t endColour = l.colourEnd ? l.colourEnd : l.colour;
            for (int i = 0; i < n; i++) {
                const double t0 = static_cast<double>(i) / n, t1 = static_cast<double>(i + 1) / n;
                pieces.push_back({l.a + (l.b - l.a) * t0, l.a + (l.b - l.a) * t1, lerpColour(l.colour, endColour, t0), l.width,
                                  lerpColour(l.colour, endColour, t1)});
            }
        }
        if (outlineFirstPiece == static_cast<size_t>(-1))
            outlineFirstPiece = pieces.size();
        for (const Line& l : pieces) {
            const float layer = lineIndex++ >= outlineFirstPiece ? 0.99975586f : 1.0f;
            const glm::vec3 a = glm::vec3(l.a - in.camera) * layer, b = glm::vec3(l.b - in.camera) * layer;
            const glm::vec3 dir = b - a;
            const float length = glm::length(dir);
            if (length < 1e-5f)
                continue;
            const glm::vec3 mid = (a + b) * 0.5f;
            glm::vec3 side = glm::cross(dir, mid);
            if (glm::length(side) < 1e-8f)
                continue;
            const float halfA = static_cast<float>(in.lineWidthPx * l.width * 0.5 * pixel * glm::length(a));
            const float halfB = static_cast<float>(in.lineWidthPx * l.width * 0.5 * pixel * glm::length(b));
            const glm::vec3 sideDir = glm::normalize(side);
            const glm::vec3 along = dir / length;
            const glm::vec3 p0 = a - along * halfA, p1 = b + along * halfB;
            const glm::vec3 sideA = sideDir * halfA, sideB = sideDir * halfB;
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            const uint32_t endColour = l.colourEnd ? l.colourEnd : l.colour;
            vertices.push_back({(p0 - sideA).x, (p0 - sideA).y, (p0 - sideA).z, l.colour, 0.0f, 0.0f});
            vertices.push_back({(p0 + sideA).x, (p0 + sideA).y, (p0 + sideA).z, l.colour, 0.0f, 0.0f});
            vertices.push_back({(p1 + sideB).x, (p1 + sideB).y, (p1 + sideB).z, endColour, 0.0f, 0.0f});
            vertices.push_back({(p1 - sideB).x, (p1 - sideB).y, (p1 - sideB).z, endColour, 0.0f, 0.0f});
            for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                indices.push_back(base + i);
        }
        lines.clear();
        if (vertices.empty())
            return;
        const uint32_t mesh = renderer->UiCreateFrameMesh(vertices.data(), static_cast<uint32_t>(vertices.size()), indices.data(),
                                                     static_cast<uint32_t>(indices.size()));
        if (!mesh)
            return;
        pendingMeshes.push_back(mesh);
        UiDraw draw;
        draw.mesh = mesh;
        draw.premultiplied = false;
        draw.blend = 1;
        draw.depth = true;
        draw.depthWrite = false;
        glm::mat4 model(1.0f);
        model[3] = glm::vec4(static_cast<float>(in.camera.x), static_cast<float>(in.camera.y), static_cast<float>(in.camera.z), 1.0f);
        std::copy_n(&model[0][0], 16, draw.model);
        renderer->UiSubmit(UiLayer::World, draw);
    }
}
