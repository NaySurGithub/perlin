#pragma once
#include <string>
#include <vector>

namespace perlin::ui {
    struct PauseScreenText {
        std::string title, back, options, quit, advancements, stats, feedback, bugs, lan, players;
        bool optionsEnabled = false;
    };
    inline constexpr const char* kPauseResumeId = "resume";
    inline constexpr const char* kPauseOptionsId = "options";
    inline constexpr const char* kPauseQuitId = "quit";
    inline constexpr const char* kPauseFeedbackId = "feedback";
    inline constexpr const char* kPausePlayersId = "players";
    inline constexpr const char* kPauseBugsId = "bugs";
    inline constexpr const char* kOptionsDoneId = "done";

    struct OptionsRow {
        std::string label;
        std::string value;
        float fraction = -1.0f;
    };

    struct OptionsLayout {
        int count = 0;
        float x[16] = {};
        float y[16] = {};
        float w = 150.0f;
        float h = 20.0f;
        float doneX = 0, doneY = 0, doneW = 200.0f;
        float titleY = 15.0f;
    };

    OptionsLayout ComputeOptionsLayout(int count, int guiW, int guiH);
    std::string BuildOptionsScreenRml(const std::vector<OptionsRow>& rows, const std::string& title, const std::string& done,
                                      int guiW, int guiH);

    std::string DefaultPauseScreenTemplate();
    std::string BuildPauseScreenRml(const std::string& templateText, const PauseScreenText& text, int guiW, int guiH);
}
