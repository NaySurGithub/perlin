#pragma once
#include "client/javashaders/PackSource.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace perlin::jshader {
    struct ShaderOption {
        enum class Kind { Toggle, Value, Const };
        std::string name;
        Kind kind = Kind::Toggle;
        bool defaultOn = false;                 // Toggle
        std::string defaultValue;               // Value / Const
        std::vector<std::string> values;        // allowed values, as written in the //[...] comment
        std::string comment;                    // free text after the define
        std::string file;                       // where it was found
        std::string type;                       // Const: int / float / bool
    };

    // Option name -> value ("true"/"false" for toggles).
    using OptionValues = std::map<std::string, std::string>;

    struct LineOrigin {
        std::string file;
        int line = 0;
    };

    struct ExpandedSource {
        std::string text;
        std::vector<LineOrigin> origins;  // one per output line
        std::set<std::string> files;      // every file that contributed
        bool ok = true;
        std::string error;
    };

    struct IncludeContext {
        const PackSource* pack = nullptr;
        const OptionValues* overrides = nullptr;
        std::vector<ShaderOption>* options = nullptr;  // receives every option found (duplicates by name are merged)
        std::set<std::string> seenOptionKeys;
    };

    // Reads `path` and recursively expands #include "..." directives, applying option overrides on the way.
    ExpandedSource ExpandIncludes(IncludeContext& ctx, const std::string& path);
    // Registers the options found in `text` without expanding includes (used to discover every option of a pack).
    void ScanOptionsInText(IncludeContext& ctx, const std::string& path, const std::string& text);
    // Expands a source string that lives at `path` (for resolving relative includes).
    ExpandedSource ExpandIncludesText(IncludeContext& ctx, const std::string& path, const std::string& text);
}
