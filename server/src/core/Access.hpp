#pragma once
#include <string>
#include <vector>

namespace perlin::server {

    // one person on a list: matched by xuid when the entry has one and the player has one, otherwise by name (case-insensitive)
    struct AccessEntry {
        std::string name;
        std::string xuid;
    };

    // ops.json / whitelist.json: [{"name": "...", "xuid": "..."}, ...]. a bare string entry is a name
    class AccessList {
    public:
        // an empty path keeps the list in memory only
        void Load(const std::string& path);
        bool Save() const;

        bool Contains(const std::string& name, const std::string& xuid) const;
        // adds or fills in the missing half of an existing entry; false when it was already there as is
        bool Add(const std::string& name, const std::string& xuid);
        // by name or xuid; true when something was removed
        bool Remove(const std::string& nameOrXuid);
        const std::vector<AccessEntry>& Entries() const { return entries; }

    private:
        std::string path;
        std::vector<AccessEntry> entries;
    };
}
