#include "client/multiplayer/CustomBlocks.hpp"

#include "client/multiplayer/NbtJson.hpp"
#include "client/resources/ServerBlocks.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <algorithm>
#include <iostream>
#include <map>

namespace perlin {
    namespace {
        using State = BlockPalette::StateValue;

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

    size_t RegisterCustomBlocks(const std::vector<proto::BlockPropertiesEntry>& blocks) {
        auto& palette = BlockPalette::Instance();
        size_t states = 0;
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
                palette.LearnState(BlockPalette::HashState(entry.name, st), entry.name, st);
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
            if (const ui::Json* components = nbt.Find("components"); components && components->IsObject()) {
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
            ServerBlocks::Instance().Add(std::move(def));
        }
        return states;
    }
}
