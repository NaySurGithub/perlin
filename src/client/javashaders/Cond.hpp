#pragma once
#include <map>
#include <string>

namespace perlin::jshader {
    using MacroMap = std::map<std::string, std::string>;

    // Evaluates a C preprocessor #if expression (integers, defined(), arithmetic/logic/comparison operators, ?:).
    // Identifiers are looked up in `macros` (their value is evaluated recursively); unknown identifiers are 0.
    long long EvalCondition(const std::string& expression, const MacroMap& macros);

    // Resolves #if/#ifdef/#ifndef/#elif/#else/#endif and #define/#undef (object-like) in `text`, the way OptiFine runs
    // its preprocessor over shaders.properties. Other '#' lines (e.g. #include, #extension) are kept as they are
    // when active. `macros` is updated by #define / #undef.
    std::string ResolveConditionals(const std::string& text, MacroMap& macros);
}
