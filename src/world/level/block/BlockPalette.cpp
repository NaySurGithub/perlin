#include "world/level/block/BlockPalette.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>

namespace perlin {
    namespace {

        void PutU16(std::string& o, size_t v) {
            o.push_back((char)(v & 0xFF));
            o.push_back((char)((v >> 8) & 0xFF));
        }

        void PutName(std::string& o, const std::string& s) {
            PutU16(o, s.size());
            o += s;
        }

        uint32_t Fnv1a(const std::string& b) {
            uint32_t h = 0x811c9dc5u;
            for (unsigned char c : b) {
                h ^= c;
                h *= 0x01000193u;
            }
            return h;
        }

    }

    int32_t BlockPalette::HashState(const std::string& name,
                                    const std::vector<std::pair<std::string, StateValue>>& states) {
        std::string nbt;
        nbt.push_back(10);
        PutName(nbt, "");
        nbt.push_back(8);
        PutName(nbt, "name");
        PutName(nbt, name);
        nbt.push_back(10);
        PutName(nbt, "states");
        for (const auto& [key, v] : states) {
            switch (v.kind) {
            case StateValue::Bool:
                nbt.push_back(1);
                PutName(nbt, key);
                nbt.push_back(v.i ? 1 : 0);
                break;
            case StateValue::Int:
                nbt.push_back(3);
                PutName(nbt, key);
                for (int b = 0; b < 4; b++)
                    nbt.push_back((char)(((uint32_t)v.i >> (8 * b)) & 0xFF));
                break;
            case StateValue::String:
                nbt.push_back(8);
                PutName(nbt, key);
                PutName(nbt, v.s);
                break;
            }
        }
        nbt.push_back(0);
        nbt.push_back(0);
        return (int32_t)Fnv1a(nbt);
    }

    int32_t BlockPalette::AirHash() {
        static const int32_t h = HashState("minecraft:air", {});
        return h;
    }

    BlockPalette::BlockPalette() : netByLocal(kMaxLocal, 0) {
        netByLocal[0] = AirHash();
        localByNet[AirHash()] = 0;
    }

    BlockPalette& BlockPalette::Instance() {
        static BlockPalette p;
        return p;
    }

    bool BlockPalette::LoadVanillaDefinitions(const std::string& path) {
        if (namesLoaded)
            return true;
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return false;
        nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
        if (j.is_discarded() || !j.contains("block_properties") || !j.contains("data_items")) {
            std::cout << "[blocks] " << path << " is not a mojang-blocks.json" << std::endl;
            return false;
        }

        struct Prop {
            StateValue::Kind kind = StateValue::Bool;
            std::vector<StateValue> values;
        };
        std::unordered_map<std::string, Prop> props;
        for (auto& p : j["block_properties"]) {
            Prop pr;
            std::string type = p.value("type", "");
            pr.kind = type == "bool"  ? StateValue::Bool
                      : type == "int" ? StateValue::Int
                                      : StateValue::String;
            for (auto& v : p["values"]) {
                StateValue sv;
                sv.kind = pr.kind;
                const auto& val = v["value"];
                if (pr.kind == StateValue::Bool)
                    sv.i = val.is_boolean() ? (val.get<bool>() ? 1 : 0) : val.get<int>() != 0;
                else if (pr.kind == StateValue::Int)
                    sv.i = val.get<int32_t>();
                else
                    sv.s = val.get<std::string>();
                pr.values.push_back(std::move(sv));
            }
            props[p["name"].get<std::string>()] = std::move(pr);
        }

        std::unordered_map<int32_t, std::string> names;
        std::unordered_map<int32_t, std::vector<std::pair<std::string, StateValue>>> states;
        std::unordered_map<std::string, std::string> rpKeys;
        std::unordered_map<std::string, std::vector<std::string>> propNames;
        size_t collisions = 0;
        for (auto& item : j["data_items"]) {
            std::string name = item.value("name", "");
            if (name.empty())
                continue;
            std::string sid = item.value("serialization_id", "");
            if (sid.rfind("tile.", 0) == 0)
                sid = sid.substr(5);
            if (!sid.empty())
                rpKeys[name] = sid;

            std::map<std::string, const Prop*> sorted;
            bool ok = true;
            std::vector<std::string> propKeys;
            for (auto& pr : item["properties"]) {
                auto it = props.find(pr["name"].get<std::string>());
                if (it == props.end() || it->second.values.empty()) {
                    ok = false;
                    break;
                }
                sorted[pr["name"].get<std::string>()] = &it->second;
                propKeys.push_back(pr["name"].get<std::string>());
            }
            if (!ok)
                continue;
            propNames[name] = propKeys;
            std::vector<std::string> keys;
            std::vector<const Prop*> list;
            for (auto& [k, p] : sorted) {
                keys.push_back(k);
                list.push_back(p);
            }
            std::vector<size_t> idx(list.size(), 0);
            for (;;) {
                std::vector<std::pair<std::string, StateValue>> st;
                st.reserve(list.size());
                for (size_t i = 0; i < list.size(); i++)
                    st.emplace_back(keys[i], list[i]->values[idx[i]]);
                int32_t h = HashState(name, st);
                auto ins = names.emplace(h, name);
                if (!ins.second && ins.first->second != name)
                    collisions++;
                if (ins.second)
                    states.emplace(h, st);
                size_t d = 0;
                while (d < idx.size() && ++idx[d] == list[d]->values.size())
                    idx[d++] = 0;
                if (d == idx.size())
                    break;
            }
        }
        if (names.empty())
            return false;
        nameByHash = std::move(names);
        statesByHash = std::move(states);
        rpKeyByName = std::move(rpKeys);
        propsByName = std::move(propNames);
        namesLoaded = true;
        std::cout << "[blocks] derived " << nameByHash.size() << " block-state ids for " << rpKeyByName.size()
                  << " blocks from " << path << " (" << collisions << " collisions)" << std::endl;
        return true;
    }

    uint16_t BlockPalette::Intern(int32_t networkId) {
        std::lock_guard<std::mutex> lk(mutex);
        auto it = localByNet.find(networkId);
        if (it != localByNet.end())
            return it->second;
        size_t n = count.load(std::memory_order_relaxed);
        if (n >= kMaxLocal)
            return 1;
        uint16_t local = (uint16_t)n;
        netByLocal[n] = networkId;
        localByNet[networkId] = local;
        count.store(n + 1, std::memory_order_release);
        return local;
    }

    int32_t BlockPalette::NetworkId(uint16_t local) const {
        if (local >= count.load(std::memory_order_acquire))
            return 0;
        return netByLocal[local];
    }

    std::string BlockPalette::Name(uint16_t local) const {
        if (local == 0)
            return "minecraft:air";
        if (!namesLoaded || local >= count.load(std::memory_order_acquire))
            return {};
        auto h = nameByHash.find(netByLocal[local]);
        return h == nameByHash.end() ? std::string() : h->second;
    }

    std::vector<std::pair<std::string, BlockPalette::StateValue>> BlockPalette::States(uint16_t local) const {
        if (local == 0 || !namesLoaded || local >= count.load(std::memory_order_acquire))
            return {};
        auto it = statesByHash.find(netByLocal[local]);
        return it == statesByHash.end() ? std::vector<std::pair<std::string, StateValue>>{} : it->second;
    }

    int BlockPalette::StateInt(uint16_t local, const std::string& key, int fallback) const {
        for (auto& [k, v] : States(local)) {
            if (k == key && (v.kind == StateValue::Int || v.kind == StateValue::Bool))
                return v.i;
        }
        return fallback;
    }

    std::string BlockPalette::StateString(uint16_t local, const std::string& key,
                                          const std::string& fallback) const {
        for (auto& [k, v] : States(local)) {
            if (k == key && v.kind == StateValue::String)
                return v.s;
        }
        return fallback;
    }

    bool BlockPalette::HasState(uint16_t local, const std::string& key) const {
        for (auto& [k, v] : States(local)) {
            (void)v;
            if (k == key)
                return true;
        }
        return false;
    }

    bool BlockPalette::BlockHasProperty(const std::string& fullName, const std::string& prop) const {
        auto it = propsByName.find(fullName);
        if (it == propsByName.end())
            return false;
        for (auto& k : it->second) {
            if (k == prop)
                return true;
        }
        return false;
    }

    std::vector<std::string> BlockPalette::AllNames() const {
        std::set<std::string> names;
        for (auto& [h, n] : nameByHash)
            names.insert(n);
        return {names.begin(), names.end()};
    }

    std::string BlockPalette::ResourcePackKey(const std::string& fullName) const {
        auto it = rpKeyByName.find(fullName);
        return it == rpKeyByName.end() ? std::string() : it->second;
    }

}
