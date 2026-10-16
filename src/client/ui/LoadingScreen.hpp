#pragma once
#include <string>

namespace perlin::ui {
    struct LoadingScreenText {
        std::string title;
        std::string message;
        std::string detail;
        float progress = -1.0f;
    };

    std::string DefaultLoadingScreenTemplate();
    std::string BuildLoadingScreenRml(const std::string& templateText, const LoadingScreenText& text, int guiW, int guiH, double time);
}
