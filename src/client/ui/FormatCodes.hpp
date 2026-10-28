#pragma once
#include <cstdint>
#include <string>

namespace perlin::ui {
    std::string FormatCodesToRml(const std::string& text, float alpha = 1.0f);
    std::string FormatCodesToRmlLines(const std::string& text);
    std::string FormatCodesToRml(const std::string& text, float alpha, bool breaks);
    std::string StripFormatCodes(const std::string& text);
    bool FormatCodeColor(char code, uint32_t& rgb);
    bool IsFormatCode(const std::string& text, size_t i);
}
