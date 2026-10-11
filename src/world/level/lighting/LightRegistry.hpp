#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin {
    constexpr int kMaxLight = 15;

    struct BlockLightProps {
        uint8_t emission = 0;
        uint8_t opacity = 0;
    };

    class LightRegistry {
    public:
        using OpacityFn = std::function<int(uint16_t)>;

        static LightRegistry& Instance();

        void LoadRules(const std::string& jsonText);
        void LoadBehaviorPack(const std::string& blocksDir);
        void SetDerivedOpacity(OpacityFn fn);
        void Invalidate();
        bool HasRules() const;

        BlockLightProps Resolve(uint16_t id) const {
            const uint16_t packed = memo[id].load(std::memory_order_relaxed);
            if (packed != kUnresolved)
                return {static_cast<uint8_t>(packed >> 8), static_cast<uint8_t>(packed & 0xFF)};
            return Compute(id);
        }

    private:
        LightRegistry();

        static constexpr uint16_t kUnresolved = 0xFFFF;

        struct Rule {
            enum class Match { Exact, Prefix, Suffix, Contains };
            Match match = Match::Exact;
            std::string pattern;
            int value = -1;
            std::string state;
            bool linear = false;
            int fallback = 0;
            std::unordered_map<std::string, int> map;
            std::vector<std::pair<std::string, std::string>> conditions;
        };

        BlockLightProps Compute(uint16_t id) const;
        int RuleValue(const std::vector<Rule>& rules, const std::unordered_map<std::string, size_t>& exact,
                      const std::string& name, uint16_t id) const;
        static void ParseRules(const void* section, std::vector<Rule>& rules,
                               std::unordered_map<std::string, size_t>& exact);
        static bool Matches(const Rule& rule, const std::string& name);
        static int Evaluate(const Rule& rule, uint16_t id);

        mutable std::array<std::atomic<uint16_t>, 65536> memo;
        mutable std::shared_mutex mutex;
        std::vector<Rule> emissionRules, opacityRules;
        std::unordered_map<std::string, size_t> emissionExact, opacityExact;
        std::unordered_map<std::string, int> packEmission, packOpacity;
        OpacityFn derivedOpacity;
        bool rulesLoaded = false;
    };
}
