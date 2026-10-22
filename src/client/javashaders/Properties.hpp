#pragma once
#include "client/javashaders/Cond.hpp"

#include <map>
#include <string>
#include <vector>

namespace perlin::jshader {
    struct Properties {
        std::vector<std::pair<std::string, std::string>> entries;  // in file order
        std::map<std::string, std::string> map;

        bool Has(const std::string& key) const { return map.count(key) != 0; }
        std::string Get(const std::string& key, const std::string& fallback = {}) const {
            const auto it = map.find(key);
            return it == map.end() ? fallback : it->second;
        }
        bool Bool(const std::string& key, bool fallback) const;
        double Number(const std::string& key, double fallback) const;
        // Entries whose key starts with `prefix` (key returned without the prefix).
        std::vector<std::pair<std::string, std::string>> WithPrefix(const std::string& prefix) const;
    };

    // Parses Java .properties text after running the conditional preprocessor with `macros`.
    Properties ParseProperties(const std::string& text, MacroMap macros, bool preprocess = true);
    std::vector<std::string> SplitWords(const std::string& value);
}
