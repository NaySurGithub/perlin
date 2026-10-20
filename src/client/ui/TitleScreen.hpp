#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace perlin::ui {
    struct ServerEntry {
        std::string name;
        std::string address;
        // Live status from a ping (filled in as soon as the server is listed or added).
        enum class Status : uint8_t { Unknown, Pinging, Online, Offline } status = Status::Unknown;
        std::string motd;    // first MOTD line (format codes stripped)
        std::string version; // game version the server reports
        int players = 0, maxPlayers = 0;
        int pingMs = 0;
        // LAN games, Realms and friends are listed with the same rows but carry their own text instead of a ping.
        enum class Kind : uint8_t { Server, Lan, Realm, Friend } kind = Kind::Server;
        std::string right; // top-right text of a non-Server row ("Open", "Online", ...)
        std::string note;  // third line of a non-Server row
        bool offline = false; // dim a Realm that is closed or a friend who is offline
        long long realmId = 0;
        std::string xuid;
    };

    enum class ServerTab : uint8_t { Saved, Lan, Realms, Friends, FriendsOffline }; // Friends = the online ones

    // One row of the account switcher.
    struct AccountRow {
        std::string id;
        std::string name;
        bool microsoft = false;
        bool active = false;
        std::string headKey; // "mchead:" image of the account's last skin (empty: none yet)
    };

    enum class TitlePage { Main, Servers, Edit, Direct, AddRealm, Accounts, AccountName, AccountSignIn };

    struct TitleModel {
        TitlePage page = TitlePage::Main;
        std::vector<ServerEntry> servers; // the saved list (the Servers tab)
        ServerTab tab = ServerTab::Saved;
        std::vector<ServerEntry> lan, realms, friends, friendsOffline;
        std::string tabMessage;  // "Scanning...", an error, or empty (shown when the tab has no rows)
        std::string joinMessage; // a line above the buttons: what happened to the last Join
        std::vector<ServerEntry>& Shown() {
            switch (tab) {
                case ServerTab::Lan: return lan;
                case ServerTab::Realms: return realms;
                case ServerTab::Friends: return friends;
                case ServerTab::FriendsOffline: return friendsOffline;
                default: return servers;
            }
        }
        const std::vector<ServerEntry>& Shown() const { return const_cast<TitleModel*>(this)->Shown(); }
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
        // Logo image and its size in GUI pixels. Perlin points this at Bedrock's textures/ui/title
        // once the assets are there; empty uses the built-in sprite.
        std::string logoSrc;
        int logoW = 256, logoH = 64;

        // Account switcher: "Logged in as ..." on the title screen, and the accounts popup.
        std::string accountLabel;    // "Logged in as <name>" (accounts popup)
        std::string accountUser;     // just the name (title screen button)
        std::string accountHead;     // head image key of the active account's skin
        std::vector<AccountRow> accounts;
        int accountScroll = 0;       // first account row shown
        std::string accountName;     // offline name being typed
        std::string signInUrl, signInCode, signInStatus;
    };

    inline constexpr const char* kTitleMulti = "title_multi";
    inline constexpr const char* kTitleQuit = "title_quit";
    inline constexpr const char* kTitleOptions = "title_options";
    inline constexpr const char* kTitleMods = "title_mods";
    inline constexpr const char* kTitleWardrobe = "title_wardrobe";
    inline constexpr const char* kServerJoin = "servers_join";
    inline constexpr const char* kServerDirect = "servers_direct";
    inline constexpr const char* kServerAdd = "servers_add";
    inline constexpr const char* kServerAddRealm = "servers_addrealm";
    inline constexpr const char* kServerEdit = "servers_edit";
    inline constexpr const char* kServerDelete = "servers_delete";
    inline constexpr const char* kServerRefresh = "servers_refresh";
    inline constexpr const char* kServerCancel = "servers_cancel";
    inline constexpr const char* kEditName = "edit_name";
    inline constexpr const char* kEditAddress = "edit_address";
    inline constexpr const char* kEditDone = "edit_done";
    inline constexpr const char* kEditCancel = "edit_cancel";
    inline constexpr const char* kTitleAccounts = "title_accounts";
    inline constexpr const char* kAccountRowPrefix = "accrow_";
    inline constexpr const char* kAccountRemovePrefix = "accrm_";
    inline constexpr const char* kAccountAddMicrosoft = "acc_add_ms";
    inline constexpr const char* kAccountAddOffline = "acc_add_off";
    inline constexpr const char* kAccountDone = "acc_done";
    inline constexpr const char* kAccountNameField = "acc_name";
    inline constexpr const char* kAccountNameDone = "acc_name_ok";
    inline constexpr const char* kAccountCancel = "acc_cancel";
    inline constexpr const char* kServerTabPrefix = "stab_";
    inline constexpr const char* kServerRowPrefix = "row_";
    inline constexpr const char* kServerJoinPrefix = "go_";

    int TitleRowHeight();
    int TitleListTop();
    int TitleListBottom(int guiH);
    int TitleVisibleRows(int guiH);
    // How many account rows fit in the accounts popup.
    int TitleAccountRows(int guiH);

    std::string BuildTitleRml(const TitleModel& model, int guiW, int guiH);
}
