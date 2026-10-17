#pragma once
#include <string>
#include <vector>

namespace perlin::ui {
    struct ServerEntry {
        std::string name;
        std::string address;
    };

    enum class TitlePage { Main, Servers, Edit, Direct };

    struct TitleModel {
        TitlePage page = TitlePage::Main;
        std::vector<ServerEntry> servers;
        int selected = -1;
        int scroll = 0;
        bool adding = false;
        std::string name;
        std::string address;
        int focus = 0;
        bool caret = false;
        int spinner = 0;
        std::string version;
        std::string copyright;
        // The panorama is drawn natively underneath (TitlePanorama); the RML only draws a
        // fallback strip when the panorama textures aren't available yet.
        bool nativePanorama = false;
    };

    inline constexpr const char* kTitleMulti = "title_multi";
    inline constexpr const char* kTitleQuit = "title_quit";
    inline constexpr const char* kTitleOptions = "title_options";
    inline constexpr const char* kServerJoin = "servers_join";
    inline constexpr const char* kServerDirect = "servers_direct";
    inline constexpr const char* kServerAdd = "servers_add";
    inline constexpr const char* kServerEdit = "servers_edit";
    inline constexpr const char* kServerDelete = "servers_delete";
    inline constexpr const char* kServerRefresh = "servers_refresh";
    inline constexpr const char* kServerCancel = "servers_cancel";
    inline constexpr const char* kEditName = "edit_name";
    inline constexpr const char* kEditAddress = "edit_address";
    inline constexpr const char* kEditDone = "edit_done";
    inline constexpr const char* kEditCancel = "edit_cancel";
    inline constexpr const char* kServerRowPrefix = "row_";
    inline constexpr const char* kServerJoinPrefix = "go_";

    int TitleRowHeight();
    int TitleListTop();
    int TitleListBottom(int guiH);
    int TitleVisibleRows(int guiH);

    std::string BuildTitleRml(const TitleModel& model, int guiW, int guiH);
}
