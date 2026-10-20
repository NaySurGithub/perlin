#pragma once
#include <string>
#include <vector>

namespace perlin::ui {
    struct LoadingScreenText {
        std::string title;
        std::string message;
        std::string detail;
        float progress = -1.0f; // shown as a percentage in the detail line by the caller
        std::string cancel;      // label of the button that drops the connection
    };
    inline constexpr const char* kLoadingCancelId = "cancel";

    // First-start asset download screen (drawn without any game textures, which aren't there yet).
    struct DownloadRow {
        std::string label;  // "Minecraft resources"
        std::string status; // "45.2 / 147.0 MB  -  18.4 MB/s"
        float progress = -1.0f; // <0: indeterminate
        bool failed = false;
    };
    struct DownloadScreenText {
        std::string title, subtitle;
        std::vector<DownloadRow> rows;
        std::string retry, skip; // buttons shown when non-empty
    };
    inline constexpr const char* kDownloadRetryId = "dl_retry";
    inline constexpr const char* kDownloadSkipId = "dl_skip";
    std::string BuildDownloadScreenRml(const DownloadScreenText& text, int guiW, int guiH, double time);

    std::string DefaultLoadingScreenTemplate();
    std::string BuildLoadingScreenRml(const std::string& templateText, const LoadingScreenText& text, int guiW, int guiH, double time);
}
