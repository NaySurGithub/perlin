#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin {

    class BlockPalette {
    public:
        static constexpr size_t kMaxLocal = 65536;

        static BlockPalette& Instance();

        static int32_t AirHash();
        struct StateValue {
            enum Kind { Bool, Int, String } kind = Bool;
            int32_t i = 0;
            std::string s;
        };
        static int32_t HashState(const std::string& name,
                                 const std::vector<std::pair<std::string, StateValue>>& sortedStates);

        bool LoadVanillaDefinitions(const std::string& mojangBlocksJsonPath);
        bool LoadServerStates(const std::string& path);
        bool LoadServerStatesText(const std::string& text, const std::string& source);
        bool NamesLoaded() const { return namesLoaded; }
        size_t StateCount() const { return nameByHash.size(); }

        uint16_t Intern(int32_t networkId);
        // Sequential runtime ids (StartGame blockNetworkIdsAreHashes=false): the id is an index into the
        // canonical block-state list (resources/block_states.json order, custom blocks appended).
        void SetSequential(bool on);
        bool Sequential() const { return sequential.load(std::memory_order_acquire); }
        void AppendSequence(int32_t hash);
        void TruncateSequence();
        uint16_t InternRuntime(int32_t runtimeId);
        int32_t RuntimeId(uint16_t local) const;
        bool LearnName(int32_t networkId, const std::string& name);
        void LearnState(int32_t networkId, const std::string& name, std::vector<std::pair<std::string, StateValue>> states);
        int32_t NetworkId(uint16_t local) const;
        std::string Name(uint16_t local) const;
        std::vector<std::pair<std::string, StateValue>> States(uint16_t local) const;
        int StateInt(uint16_t local, const std::string& key, int fallback) const;
        std::string StateString(uint16_t local, const std::string& key, const std::string& fallback) const;
        bool HasState(uint16_t local, const std::string& key) const;
        size_t Count() const { return count.load(std::memory_order_acquire); }

        std::vector<std::string> AllNames() const;
        std::string ResourcePackKey(const std::string& fullName) const;
        bool BlockHasProperty(const std::string& fullName, const std::string& prop) const;

    private:
        BlockPalette();

        bool namesLoaded = false;
        std::unordered_map<int32_t, std::string> nameByHash;
        std::unordered_map<int32_t, std::vector<std::pair<std::string, StateValue>>> statesByHash;
        std::unordered_map<std::string, std::string> rpKeyByName;
        std::unordered_map<std::string, std::vector<std::string>> propsByName;

        mutable std::mutex mutex;
        mutable std::mutex learnedMutex;
        std::unordered_map<int32_t, std::string> learned;
        std::unordered_map<int32_t, std::vector<std::pair<std::string, StateValue>>> learnedStates;
        std::unordered_map<int32_t, uint16_t> localByNet;
        std::atomic<bool> sequential{false};
        std::vector<int32_t> sequence;
        std::unordered_map<int32_t, int32_t> sequenceIndex;
        bool sequenceFrozen = false;
        size_t vanillaSequence = 0;
        std::vector<int32_t> netByLocal;
        std::atomic<size_t> count{1};
    };

}
