#pragma once
#include <string>

namespace perlin::ui {
    struct PauseScreenText {
        std::string title, back, options, quit, advancements, stats, feedback, bugs, lan;
        bool optionsEnabled = false;
    };
    inline constexpr const char* kPauseResumeId = "resume";
    inline constexpr const char* kPauseOptionsId = "options";
    inline constexpr const char* kPauseQuitId = "quit";

    std::string DefaultPauseScreenTemplate();
    std::string BuildPauseScreenRml(const std::string& templateText, const PauseScreenText& text, int guiW, int guiH);
}
