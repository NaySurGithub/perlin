#include "world/level/lighting/LightRegistry.hpp"

#include "world/level/block/BlockPalette.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace perlin {
    namespace {
        std::string StateText(uint16_t id, const std::string& key) {
            for (const auto& [name, value] : BlockPalette::Instance().States(id)) {
                if (name != key)
                    continue;
                switch (value.kind) {
                case BlockPalette::StateValue::Bool:
                    return value.i ? "true" : "false";
                case BlockPalette::StateValue::Int:
                    return std::to_string(value.i);
                default:
                    return value.s;
                }
            }
            return {};
        }

        int Clamp(int v) {
            return std::clamp(v, 0, kMaxLight);
        }

        int FromComponent(const nlohmann::json& v, bool unit) {
            if (!v.is_number())
                return -1;
            const double d = v.get<double>();
            return Clamp(static_cast<int>(unit ? d * kMaxLight + 0.5 : d + 0.5));
        }
    }

    LightRegistry& LightRegistry::Instance() {
        static LightRegistry registry;
        return registry;
    }

    LightRegistry::LightRegistry() {
        for (auto& slot : memo)
            slot.store(kUnresolved, std::memory_order_relaxed);
    }

    void LightRegistry::Invalidate() {
        std::unique_lock lock(mutex);
        for (auto& slot : memo)
            slot.store(kUnresolved, std::memory_order_relaxed);
    }

    void LightRegistry::SetDerivedOpacity(OpacityFn fn) {
        std::unique_lock lock(mutex);
        derivedOpacity = std::move(fn);
        for (auto& slot : memo)
            slot.store(kUnresolved, std::memory_order_relaxed);
    }

    bool LightRegistry::HasRules() const {
        std::shared_lock lock(mutex);
        return rulesLoaded;
    }

    void LightRegistry::ParseRules(const void* section, std::vector<Rule>& rules,
                                   std::unordered_map<std::string, size_t>& exact) {
        const auto& obj = *static_cast<const nlohmann::ordered_json*>(section);
        if (!obj.is_object())
            return;
        for (const auto& [key, value] : obj.items()) {
            Rule rule;
            std::string pattern = key;
            const bool front = !pattern.empty() && pattern.front() == '*';
            const bool back = pattern.size() > 1 && pattern.back() == '*';
            if (front)
                pattern.erase(0, 1);
            if (back)
                pattern.pop_back();
            rule.pattern = pattern;
            rule.match = front && back ? Rule::Match::Contains
                         : front       ? Rule::Match::Suffix
                         : back        ? Rule::Match::Prefix
                                       : Rule::Match::Exact;
            if (value.is_number()) {
                rule.value = Clamp(value.get<int>());
            } else if (value.is_object()) {
                rule.state = value.value("state", "");
                rule.linear = value.value("linear", false);
                rule.fallback = Clamp(value.value("default", 0));
                if (value.contains("map") && value["map"].is_object())
                    for (const auto& [k, v] : value["map"].items())
                        if (v.is_number())
                            rule.map[k] = Clamp(v.get<int>());
                if (value.contains("requires") && value["requires"].is_object())
                    for (const auto& [k, v] : value["requires"].items())
                        rule.conditions.emplace_back(k, v.is_string() ? v.get<std::string>() : v.dump());
            } else {
                continue;
            }
            if (rule.match == Rule::Match::Exact)
                exact[rule.pattern] = rules.size();
            rules.push_back(std::move(rule));
        }
    }

    void LightRegistry::LoadRules(const std::string& jsonText) {
        const auto root = nlohmann::ordered_json::parse(jsonText, nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            std::cout << "[light] light.json is not valid JSON" << std::endl;
            return;
        }
        std::vector<Rule> emission, opacity;
        std::unordered_map<std::string, size_t> emissionIndex, opacityIndex;
        if (root.contains("emission"))
            ParseRules(&root["emission"], emission, emissionIndex);
        if (root.contains("opacity"))
            ParseRules(&root["opacity"], opacity, opacityIndex);

        std::unique_lock lock(mutex);
        emissionRules = std::move(emission);
        opacityRules = std::move(opacity);
        emissionExact = std::move(emissionIndex);
        opacityExact = std::move(opacityIndex);
        rulesLoaded = true;
        for (auto& slot : memo)
            slot.store(kUnresolved, std::memory_order_relaxed);
    }

    void LightRegistry::LoadBehaviorPack(const std::string& blocksDir) {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::is_directory(blocksDir, ec))
            return;

        std::unordered_map<std::string, int> emission, opacity;
        for (const auto& entry : fs::recursive_directory_iterator(blocksDir, ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json")
                continue;
            std::ifstream file(entry.path(), std::ios::binary);
            const auto root = nlohmann::json::parse(file, nullptr, false, true);
            if (root.is_discarded() || !root.contains("minecraft:block"))
                continue;
            const auto& block = root["minecraft:block"];
            if (!block.contains("description") || !block["description"].contains("identifier"))
                continue;
            const std::string identifier = block["description"]["identifier"].get<std::string>();

            auto read = [&](const nlohmann::json& components) {
                if (!components.is_object())
                    return;
                if (components.contains("minecraft:light_emission")) {
                    const int v = FromComponent(components["minecraft:light_emission"], false);
                    if (v >= 0)
                        emission[identifier] = std::max(emission[identifier], v);
                }
                if (components.contains("minecraft:block_light_emission")) {
                    const int v = FromComponent(components["minecraft:block_light_emission"], true);
                    if (v >= 0)
                        emission[identifier] = std::max(emission[identifier], v);
                }
                if (components.contains("minecraft:light_dampening")) {
                    const int v = FromComponent(components["minecraft:light_dampening"], false);
                    if (v >= 0)
                        opacity[identifier] = v;
                }
                if (components.contains("minecraft:block_light_filter")) {
                    const int v = FromComponent(components["minecraft:block_light_filter"], false);
                    if (v >= 0)
                        opacity[identifier] = v;
                }
            };
            if (block.contains("components"))
                read(block["components"]);
            if (block.contains("permutations") && block["permutations"].is_array())
                for (const auto& permutation : block["permutations"])
                    if (permutation.contains("components"))
                        read(permutation["components"]);
        }

        std::unique_lock lock(mutex);
        packEmission = std::move(emission);
        packOpacity = std::move(opacity);
        for (auto& slot : memo)
            slot.store(kUnresolved, std::memory_order_relaxed);
        if (!packEmission.empty() || !packOpacity.empty())
            std::cout << "[light] behavior pack: " << packEmission.size() << " emitters, "
                      << packOpacity.size() << " opacity overrides" << std::endl;
    }

    bool LightRegistry::Matches(const Rule& rule, const std::string& name) {
        switch (rule.match) {
        case Rule::Match::Exact:
            return name == rule.pattern;
        case Rule::Match::Prefix:
            return name.compare(0, rule.pattern.size(), rule.pattern) == 0;
        case Rule::Match::Suffix:
            return name.size() >= rule.pattern.size() &&
                   name.compare(name.size() - rule.pattern.size(), rule.pattern.size(), rule.pattern) == 0;
        case Rule::Match::Contains:
            return name.find(rule.pattern) != std::string::npos;
        }
        return false;
    }

    int LightRegistry::Evaluate(const Rule& rule, uint16_t id) {
        if (rule.value >= 0)
            return rule.value;
        for (const auto& [key, expected] : rule.conditions)
            if (StateText(id, key) != expected)
                return rule.fallback;
        if (rule.state.empty())
            return rule.fallback;
        const std::string text = StateText(id, rule.state);
        if (rule.linear)
            return text.empty() ? rule.fallback : Clamp(std::atoi(text.c_str()));
        const auto it = rule.map.find(text);
        return it == rule.map.end() ? rule.fallback : it->second;
    }

    int LightRegistry::RuleValue(const std::vector<Rule>& rules,
                                 const std::unordered_map<std::string, size_t>& exact,
                                 const std::string& name, uint16_t id) const {
        if (const auto it = exact.find(name); it != exact.end())
            return Evaluate(rules[it->second], id);
        for (const Rule& rule : rules)
            if (rule.match != Rule::Match::Exact && Matches(rule, name))
                return Evaluate(rule, id);
        return -1;
    }

    BlockLightProps LightRegistry::Compute(uint16_t id) const {
        BlockLightProps props;
        if (id == 0) {
            memo[id].store(0, std::memory_order_relaxed);
            return props;
        }

        const std::string name = BlockPalette::Instance().Name(id);
        std::shared_lock lock(mutex);

        int emission = -1, opacity = -1;
        if (const auto it = packEmission.find(name); it != packEmission.end())
            emission = it->second;
        if (const auto it = packOpacity.find(name); it != packOpacity.end())
            opacity = it->second;
        if (emission < 0)
            emission = RuleValue(emissionRules, emissionExact, name, id);
        if (opacity < 0)
            opacity = RuleValue(opacityRules, opacityExact, name, id);

        if (opacity < 0 && derivedOpacity)
            opacity = derivedOpacity(id);

        props.emission = static_cast<uint8_t>(std::max(emission, 0));
        props.opacity = static_cast<uint8_t>(opacity < 0 ? kMaxLight : Clamp(opacity));

        if (rulesLoaded && derivedOpacity)
            memo[id].store(static_cast<uint16_t>((props.emission << 8) | props.opacity),
                           std::memory_order_relaxed);
        return props;
    }
}
