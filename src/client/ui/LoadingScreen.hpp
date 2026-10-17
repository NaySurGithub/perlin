#pragma once
#include <string>

namespace perlin::ui {
    struct LoadingScreenText {
        std::string title;
        std::string message;
        std::string detail;
        float progress = -1.0f; // shown as a percentage in the detail line by the caller
        std::string cancel;      // label of the button that drops the connection
    };
    inline constexpr const char* kLoadingCancelId = "cancel";

    std::string DefaultLoadingScreenTemplate();
    std::string BuildLoadingScreenRml(const std::string& templateText, const LoadingScreenText& text, int guiW, int guiH, double time);
}
