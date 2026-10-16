#include "client/multiplayer/CustomBlocks.hpp"

#include "client/multiplayer/NbtJson.hpp"
#include "client/resources/ServerBlocks.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "world/level/lighting/LightRegistry.hpp"
#include "world/level/chunk/ChunkCodec.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>

namespace perlin {
    namespace {
        using State = BlockPalette::StateValue;

        int32_t ResolvePersistent(const PersistentBlockState& block) {
            std::vector<std::pair<std::string, State>> states;
            states.reserve(block.states.size());
            for (const auto& e : block.states) {
                State v;
                v.kind = e.kind == 0 ? State::Bool : e.kind == 1 ? State::Int : State::String;
                v.i = e.number;
                v.s = e.text;
                states.emplace_back(e.key, std::move(v));
            }
            std::sort(states.begin(), states.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            std::string name = block.name;
            if (!name.empty() && name.find(':') == std::string::npos)
                name = "minecraft:" + name;
            const int32_t hash = BlockPalette::HashState(name, states);
            BlockPalette::Instance().LearnState(hash, name, std::move(states));
            return hash;
        }


        float Number(const ui::Json* v, float fallback) {
            return v && v->IsNumber() ? static_cast<float>(v->NumberOr(fallback)) : fallback;
        }

        bool Vec3(const ui::Json* v, float out[3]) {
            if (!v || !v->IsArray() || v->array.size() < 3)
                return false;
            for (int i = 0; i < 3; i++)
                out[i] = Number(&v->array[static_cast<size_t>(i)], 0.0f);
            return true;
        }

        bool ParseBox(const ui::Json& o, CollisionBox& box) {
            float origin[3] = {-8, 0, -8}, size[3] = {16, 16, 16};
            if (!o.IsObject())
                return false;
            if (const ui::Json* e = o.Find("enabled"); e && (e->IsNumber() ? e->NumberOr(1) == 0 : (e->IsBool() && !e->boolean)))
                return false;
            Vec3(o.Find("origin"), origin);
            Vec3(o.Find("size"), size);
            box = {(origin[0] + 8) / 16.0f, origin[1] / 16.0f, (origin[2] + 8) / 16.0f,
                   (origin[0] + 8 + size[0]) / 16.0f, (origin[1] + size[1]) / 16.0f, (origin[2] + 8 + size[2]) / 16.0f};
            return true;
        }

        CollisionSet ParseCollision(const ui::Json* v) {
            CollisionSet set;
            if (!v)
                return set;
            if (v->IsBool()) {
                set.defined = true;
                if (v->boolean)
                    set.boxes.push_back({0, 0, 0, 1, 1, 1});
                return set;
            }
            if (v->IsNumber()) {
                set.defined = true;
                if (v->NumberOr(1) != 0)
                    set.boxes.push_back({0, 0, 0, 1, 1, 1});
                return set;
            }
            set.defined = true;
            if (v->IsArray()) {
                for (const ui::Json& e : v->array) {
                    CollisionBox box;
                    if (ParseBox(e, box))
                        set.boxes.push_back(box);
                }
            } else if (v->IsObject()) {
                if (const ui::Json* boxes = v->Find("boxes"); boxes && boxes->IsArray()) {
                    for (const ui::Json& e : boxes->array) {
                        CollisionBox box;
                        if (ParseBox(e, box))
                            set.boxes.push_back(box);
                    }
                } else {
                    CollisionBox box;
                    if (ParseBox(*v, box))
                        set.boxes.push_back(box);
                }
            }
            return set;
        }

        int ParseLevel(const ui::Json* v, const char* key, bool fraction) {
            if (!v)
                return -1;
            const ui::Json* n = v;
            if (v->IsObject())
                n = v->Find(key);
            if (!n || !n->IsNumber())
                return -1;
            double d = n->NumberOr(0);
            if (fraction && d <= 1.0)
                d *= 15.0;
            return std::clamp(static_cast<int>(std::lround(d)), 0, 15);
        }

        struct Prop {
            std::string name;
            std::vector<State> values;
        };

        State ToState(const ui::Json& v) {
            State s;
            if (v.IsString()) {
                s.kind = State::String;
                s.s = v.string;
            } else if (v.IsBool()) {
                s.kind = State::Bool;
                s.i = v.boolean ? 1 : 0;
            } else {
                s.kind = State::Int;
                s.i = static_cast<int32_t>(v.NumberOr(0));
            }
            return s;
        }
    }

    namespace {
        const ui::Json* FindKey(const ui::Json& node, const std::string& key, int depth = 0) {
            if (depth > 6 || !node.IsObject())
                return nullptr;
            if (const ui::Json* hit = node.Find(key))
                return hit;
            for (const auto& [k, v] : node.object)
                if (const ui::Json* hit = FindKey(v, key, depth + 1))
                    return hit;
            return nullptr;
        }

        std::string IconKey(const ui::Json* icon) {
            if (!icon)
                return {};
            if (icon->IsString())
                return icon->string;
            if (!icon->IsObject())
                return {};
            if (const ui::Json* textures = icon->Find("textures"); textures && textures->IsObject())
                if (const ui::Json* d = textures->Find("default"); d && d->IsString())
                    return d->string;
            if (const ui::Json* t = icon->Find("texture"); t && t->IsString())
                return t->string;
            return {};
        }
    }

    size_t RegisterCustomItems(const std::vector<proto::ItemstatesEntry>& items) {
        ServerBlocks::Instance().ClearItems();
        size_t count = 0;
        for (const proto::ItemstatesEntry& item : items) {
            if (item.name.rfind("minecraft:", 0) == 0 || item.nbt.bytes.empty())
                continue;
            ui::Json nbt;
            if (!NetworkNbtToJson(item.nbt.bytes, nbt) || !nbt.IsObject())
                continue;
            CustomItemDef def;
            def.name = item.name;
            def.icon = IconKey(FindKey(nbt, "minecraft:icon"));
            if (def.icon.empty())
                def.icon = IconKey(FindKey(nbt, "icon"));
            if (const ui::Json* dn = FindKey(nbt, "minecraft:display_name")) {
                if (dn->IsString())
                    def.displayName = dn->string;
                else if (const ui::Json* v = dn->Find("value"); v && v->IsString())
                    def.displayName = v->string;
            }
            ServerBlocks::Instance().SetItem(std::move(def));
            count++;
        }
        return count;
    }

    void InstallPersistentChunkResolver() { SetPersistentResolver(&ResolvePersistent); }

    size_t RegisterCustomBlocks(const std::vector<proto::BlockPropertiesEntry>& blocks) {
        auto& palette = BlockPalette::Instance();
        size_t states = 0;
        palette.TruncateSequence();
        ServerBlocks::Instance().Reset();
        LightRegistry::Instance().ClearCustom();
        for (const proto::BlockPropertiesEntry& entry : blocks) {
            ui::Json nbt;
            if (entry.name.empty() || !NetworkNbtToJson(entry.state.bytes, nbt) || !nbt.IsObject())
                continue;
            std::map<std::string, Prop> sorted;
            if (const ui::Json* props = nbt.Find("properties"); props && props->IsArray())
                for (const ui::Json& p : props->array) {
                    const ui::Json* name = p.Find("name");
                    const ui::Json* values = p.Find("enum");
                    if (!name || !name->IsString() || !values || !values->IsArray() || values->array.empty())
                        continue;
                    Prop prop;
                    prop.name = name->string;
                    for (const ui::Json& v : values->array)
                        prop.values.push_back(ToState(v));
                    sorted[prop.name] = std::move(prop);
                }
            std::vector<const Prop*> list;
            for (const auto& [k, p] : sorted)
                list.push_back(&p);
            std::vector<size_t> idx(list.size(), 0);
            size_t emitted = 0;
            for (;;) {
                std::vector<std::pair<std::string, State>> st;
                st.reserve(list.size());
                for (size_t i = 0; i < list.size(); i++)
                    st.emplace_back(list[i]->name, list[i]->values[idx[i]]);
                const int32_t stateHash = BlockPalette::HashState(entry.name, st);
                palette.LearnState(stateHash, entry.name, st);
                palette.AppendSequence(stateHash);
                states++;
                if (++emitted >= 16384)
                    break;
                size_t d = 0;
                while (d < idx.size() && ++idx[d] == list[d]->values.size())
                    idx[d++] = 0;
                if (d == idx.size())
                    break;
            }

            CustomBlockDef def;
            def.name = entry.name;
            def.json = nbt.Dump();
            CustomBlockCollision collision;
            int emission = -1, dampening = -1;
            if (const ui::Json* components = nbt.Find("components"); components && components->IsObject()) {
                collision.base = ParseCollision(components->Find("minecraft:collision_box"));
                int level = ParseLevel(components->Find("minecraft:light_emission"), "emission", false);
                if (level < 0)
                    level = ParseLevel(components->Find("minecraft:block_light_emission"), "emission", true);
                emission = level;
                int damp = ParseLevel(components->Find("minecraft:light_dampening"), "lightLevel", false);
                if (damp < 0)
                    damp = ParseLevel(components->Find("minecraft:block_light_filter"), "lightLevel", false);
                dampening = damp;
                if (const ui::Json* mi = components->Find("minecraft:material_instances"); mi && mi->IsObject()) {
                    const ui::Json* mats = mi->Find("materials");
                    const ui::Json* map = mats && mats->IsObject() ? mats : mi;
                    for (const auto& [face, v] : map->object) {
                        if (!v.IsObject())
                            continue;
                        if (const ui::Json* tex = v.Find("texture"); tex && tex->IsString())
                            def.faceTexture[face] = tex->string;
                        if (const ui::Json* rm = v.Find("render_method"); rm && rm->IsString() && rm->string != "opaque")
                            def.cutout = true;
                    }
                }
            }
            if (const ui::Json* perms = nbt.Find("permutations"); perms && perms->IsArray())
                for (const ui::Json& perm : perms->array) {
                    const ui::Json* cond = perm.Find("condition");
                    const ui::Json* comps = perm.Find("components");
                    if (!cond || !cond->IsString() || !comps || !comps->IsObject())
                        continue;
                    CollisionSet set = ParseCollision(comps->Find("minecraft:collision_box"));
                    if (set.defined)
                        collision.permutations.emplace_back(cond->string, std::move(set));
                }
            if (collision.base.defined || !collision.permutations.empty())
                ServerBlocks::Instance().SetCollision(entry.name, std::move(collision));
            if (emission >= 0 || dampening >= 0)
                LightRegistry::Instance().SetCustom(entry.name, emission, dampening);
            ServerBlocks::Instance().Add(std::move(def));
        }
        return states;
    }
}
