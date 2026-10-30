#include "core/Access.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace perlin::server {
    namespace {
        std::string Lower(std::string s) {
            for (char& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }
    }

    void AccessList::Load(const std::string& file) {
        path = file;
        entries.clear();
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return;
        std::stringstream buf;
        buf << in.rdbuf();
        json::Value root;
        if (!json::Parse(buf.str(), root) || !root.IsArray())
            return;
        for (const json::Value& e : root.items) {
            AccessEntry entry;
            if (e.IsString())
                entry.name = e.text;
            else {
                entry.name = e["name"].String();
                entry.xuid = e["xuid"].String();
            }
            if (!entry.name.empty() || !entry.xuid.empty())
                entries.push_back(std::move(entry));
        }
    }

    bool AccessList::Save() const {
        if (path.empty())
            return true;
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << "[";
        for (size_t i = 0; i < entries.size(); i++)
            out << (i ? "," : "") << "\n  {\"name\": \"" << json::Escape(entries[i].name) << "\", \"xuid\": \"" << json::Escape(entries[i].xuid) << "\"}";
        out << (entries.empty() ? "]\n" : "\n]\n");
        return out.good();
    }

    bool AccessList::Contains(const std::string& name, const std::string& xuid) const {
        const std::string lowered = Lower(name);
        for (const AccessEntry& e : entries) {
            if (!xuid.empty() && !e.xuid.empty() && e.xuid == xuid)
                return true;
            if (!e.name.empty() && Lower(e.name) == lowered)
                return true;
            // an entry that is only an xuid (a name field holding digits works too)
            if (!xuid.empty() && e.name == xuid)
                return true;
        }
        return false;
    }

    bool AccessList::Add(const std::string& name, const std::string& xuid) {
        const std::string lowered = Lower(name);
        for (AccessEntry& e : entries) {
            const bool same = (!xuid.empty() && e.xuid == xuid) || (!name.empty() && Lower(e.name) == lowered);
            if (!same)
                continue;
            bool changed = false;
            if (e.xuid.empty() && !xuid.empty()) {
                e.xuid = xuid;
                changed = true;
            }
            if (e.name.empty() && !name.empty()) {
                e.name = name;
                changed = true;
            }
            if (changed)
                Save();
            return changed;
        }
        entries.push_back({name, xuid});
        Save();
        return true;
    }

    bool AccessList::Remove(const std::string& key) {
        const std::string lowered = Lower(key);
        const size_t before = entries.size();
        entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const AccessEntry& e) { return Lower(e.name) == lowered || (!e.xuid.empty() && e.xuid == key); }),
                      entries.end());
        const bool removed = entries.size() != before;
        if (removed)
            Save();
        return removed;
    }
}
