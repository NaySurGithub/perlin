#pragma once
#include <cstdint>
#include <string>

namespace perlin::ui {
    std::string LoadUiTemplate(const std::string& dataDir, const std::string& fileName, const std::string& defaultText,
                               uint64_t& version);
    void ReplaceAll(std::string& text, const std::string& key, const std::string& value);
    std::string ScreenTemplate(const std::string& fileName, const std::string& defaultText);

    struct ScreenTemplateDefault {
        ScreenTemplateDefault(const char* fileName, std::string defaultText);
    };
    void ExportScreenTemplates();
}
