#pragma once
#include <string>
#include <vector>

namespace perlin::ui {
    struct PauseScreenText {
        std::string title, back, options, quit, advancements, stats, feedback, bugs, lan, players, world;
        bool optionsEnabled = false;
        bool showPanel = false;
        std::string panelTitle, roleOperator, roleMember, roleVisitor;
        struct PanelRow {
            std::string name;
            int perm = -1;
        };
        std::vector<PanelRow> panel;
    };
    inline constexpr const char* kPauseResumeId = "resume";
    inline constexpr const char* kPauseOptionsId = "options";
    inline constexpr const char* kPauseQuitId = "quit";
    inline constexpr const char* kPauseFeedbackId = "feedback";
    inline constexpr const char* kPausePlayersId = "players";
    inline constexpr const char* kPauseWorldId = "world";
    inline constexpr const char* kPauseBugsId = "bugs";
    inline constexpr const char* kOptionsDoneId = "done";

    struct KeybindRow {
        bool header = false;
        std::string label, value;
        int color = 0;
        bool canReset = false;
    };
    struct KeybindLayout {
        float listTop = 32, listBottom = 0, pitch = 24, labelX = 0, keyX = 0, keyW = 75, resetX = 0, resetW = 50, doneX = 0, doneY = 0, doneW = 200;
        float contentH = 0, maxScroll = 0;
        float RowY(int i, float scroll) const { return listTop + 6.0f + static_cast<float>(i) * pitch - scroll; }
    };
    KeybindLayout ComputeKeybindLayout(int rows, int guiW, int guiH);
    std::string BuildKeybindsScreenRml(const std::vector<KeybindRow>& rows, const std::string& title, const std::string& done,
                                       const std::string& reset, float scroll, int guiW, int guiH);
    inline constexpr const char* kKeybindDoneId = "kbdone";

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
