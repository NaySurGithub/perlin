#pragma once
#include <string>
#include <vector>

namespace perlin::ui {
    struct PauseScreenText {
        std::string title, back, options, quit, advancements, stats, feedback, bugs, lan, players, world, mods;
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
    inline constexpr const char* kPauseWardrobeId = "wardrobe";
    inline constexpr const char* kPauseModsId = "mods";
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
        bool header = false;
    };

    struct OptionsLayout {
        int count = 0;
        std::vector<float> x, y;
        std::vector<char> visible;
        float w = 150.0f;
        float h = 20.0f;
        float doneX = 0, doneY = 0, doneW = 200.0f;
        float titleY = 15.0f;
        float listTop = 32.0f, listBottom = 0.0f, contentH = 0.0f, maxScroll = 0.0f;
        bool scrolling = false;
    };

    OptionsLayout ComputeOptionsLayout(const std::vector<OptionsRow>& rows, int guiW, int guiH, float scroll);
    std::string BuildOptionsScreenRml(const std::vector<OptionsRow>& rows, const std::string& title, const std::string& done,
                                      float scroll, int guiW, int guiH);

    struct ListButton {
        std::string text;
        float w = 60.0f;
        bool enabled = true;
        int color = 0;
    };
    struct ListRow {
        bool header = false;
        std::string label, sub;
        int labelColor = 0;
        std::vector<ListButton> buttons;
    };
    struct ListLayout {
        float listTop = 32, listBottom = 0, pitch = 24, labelX = 0, rightX = 0, doneX = 0, doneY = 0, doneW = 200, contentH = 0, maxScroll = 0;
        float RowY(int i, float scroll) const { return listTop + 6.0f + static_cast<float>(i) * pitch - scroll; }
        float ButtonX(const ListRow& row, int index) const;
    };
    ListLayout ComputeListLayout(int rows, int guiW, int guiH);
    std::string BuildListScreenRml(const std::vector<ListRow>& rows, const std::string& title, const std::string& done, float scroll,
                                   int guiW, int guiH);

    struct WorldRow {
        enum class Kind { Section, Toggle, Segmented, Slider, Number, Button } kind = Kind::Section;
        std::string label, desc;
        bool on = false;
        std::vector<std::string> options;
        int selected = 0;
        float fraction = 0.0f;
        std::string value;
        bool editing = false;
        bool enabled = true;
    };
    struct WorldLayout {
        float panelX = 0, panelW = 0, listTop = 0, listBottom = 0, contentH = 0, maxScroll = 0, doneX = 0, doneY = 0, doneW = 200;
        std::vector<float> y, h;
    };
    WorldLayout ComputeWorldLayout(const std::vector<WorldRow>& rows, int guiW, int guiH, float scroll);
    std::string BuildWorldScreenRml(const std::vector<WorldRow>& rows, const std::string& title, const std::string& done, float scroll,
                                    int guiW, int guiH);

    std::string DefaultPauseScreenTemplate();
    std::string BuildPauseScreenRml(const std::string& templateText, const PauseScreenText& text, int guiW, int guiH);
}
