#pragma once
#include <string>
#include <vector>

namespace perlin::ui {
    // Docked control panel shown while a replay is open (Esc toggles it).
    struct ReplayBarModel {
        bool open = false;       // full panel; otherwise only a small status line
        bool paused = false;
        bool ended = false;
        bool catching = false;   // fast-forwarding after a seek
        bool hideHud = true;
        bool hudShown = false;   // "UI hidden with F1" mode: draw nothing
        int cameraMode = 0;      // 0 free, 1 follow recorded player, 2 keyframes
        double timeMs = 0, durationMs = 0;
        double scrubMs = -1;     // >= 0 while dragging the timeline
        double speed = 1.0;
        double inMs = -1, outMs = -1;
        std::string title, status;
        struct Mark {
            double timeMs = 0;
            unsigned color = 0xFFFFFF;
            std::string name;
        };
        std::vector<Mark> markers;
        struct Key {
            double timeMs = 0;
            int track = 0;
        };
        std::vector<Key> keys;
        bool previewKeys = false;
    };
    struct ReplayBarLayout {
        float x = 0, y = 0, w = 0, h = 0;
        float trackX = 0, trackY = 0, trackW = 0, trackH = 0;
    };
    ReplayBarLayout ComputeReplayBarLayout(int guiW, int guiH);
    std::string BuildReplayBarRml(const ReplayBarModel& m, int guiW, int guiH);
    std::string FormatReplayTime(double ms);

    inline constexpr const char* kRbTrack = "rb_track";
    inline constexpr const char* kRbPlay = "rb_play";
    inline constexpr const char* kRbBack = "rb_back";
    inline constexpr const char* kRbFwd = "rb_fwd";
    inline constexpr const char* kRbSlower = "rb_slower";
    inline constexpr const char* kRbFaster = "rb_faster";
    inline constexpr const char* kRbSpeed = "rb_speed";
    inline constexpr const char* kRbCamera = "rb_camera";
    inline constexpr const char* kRbMarker = "rb_marker";
    inline constexpr const char* kRbHud = "rb_hud";
    inline constexpr const char* kRbOptions = "rb_options";
    inline constexpr const char* kRbExport = "rb_export";
    inline constexpr const char* kRbKeyframes = "rb_keyframes";
    inline constexpr const char* kRbExit = "rb_exit";

    // Generic settings panel (export dialog, keyframe editor) drawn with the same widgets.
    struct PanelRow {
        enum class Kind { Header, Cycle, Field, Toggle, Button, Info } kind = Kind::Info;
        std::string id, label, value;
        bool enabled = true, focused = false, on = false;
    };
    struct PanelButton {
        std::string id, label;
        bool enabled = true;
    };
    struct PanelModel {
        std::string title, status;
        std::vector<std::string> tabs; // clickable ids "pn_tab0".."pn_tabN"
        int tab = 0;
        std::vector<PanelRow> rows;
        std::vector<PanelButton> buttons; // bottom row
        int scroll = 0; // first visible row
        bool caret = false;
        float progress = -1; // >=0 draws a progress bar above the buttons
    };
    struct PanelLayout {
        float x = 0, y = 0, w = 0, h = 0;
        float rowsTop = 0, rowH = 22;
        int visibleRows = 1;
    };
    PanelLayout ComputePanelLayout(const PanelModel& m, int guiW, int guiH);
    std::string BuildPanelRml(const PanelModel& m, int guiW, int guiH);
    inline constexpr const char* kPanelTabPrefix = "pn_tab";
}
