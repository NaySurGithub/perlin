#pragma once
#include <cstdint>
#include <string>

namespace perlin::ui {
    std::string FormatCodesToRml(const std::string& text);
    std::string StripFormatCodes(const std::string& text);
    bool FormatCodeColor(char code, uint32_t& rgb);
}
