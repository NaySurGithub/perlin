#include "client/ui/TitleScreen.hpp"

#include "client/ui/RmlEmit.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace perlin::ui {
    namespace {
        std::string N(int v) { return std::to_string(v); }

        constexpr int kRowW = 305;
        constexpr int kRowH = 36;

        const char* kStyle = R"(<rml>
<head>
<title>title</title>
<style>
body { position: absolute; left: 0px; top: 0px; margin: 0; padding: 0; background: transparent; }
div { display: block; position: absolute; }
mcimg { display: block; position: absolute; }
.btn { height: 20px; z-index: 5; }
.btn .h { display: none; }
.btn:hover .h { display: block; }
.btn .lbl { left: 0px; top: 6px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; }
.btn.off .lbl { color: #a0a0a0; }
.txt { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; color: #ffffff; white-space: nowrap; z-index: 4; }
.gray { color: #808080; }
.dim { color: #a0a0a0; }
.ctr { text-align: center; }
.row { z-index: 3; }
.row .go { display: none; }
.row:hover .go { display: block; }
.field { height: 20px; z-index: 5; }
.field .val { left: 4px; top: 6px; height: 9px; overflow: hidden; }
.green { color: #55ff55; }
.yellow { color: #ffff55; }
.red { color: #ff5555; }
.dark { color: #606060; }
.acc { z-index: 13; }
.acc .hl { display: none; }
.acc:hover .hl { display: block; }
.sbtn { height: 20px; z-index: 15; }
.sbtn .h { display: none; }
.sbtn:hover .h { display: block; }
.sbtn .lbl { left: 0px; top: 6px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; }
@keyframes pano { from { left: 0px; } to { left: -{{span}}px; } }
</style>
</head>
<body style="width: {{w}}px; height: {{h}}px;">
)";

        std::string Btn(const std::string& id, int x, int y, int w, const std::string& label, bool off = false) {
            std::string d = "<div" + std::string(off ? "" : " id=\"" + id + "\"") + " class=\"btn" + (off ? " off" : "") + "\" style=\"left:" + N(x) + "px;top:" + N(y) +
                            "px;width:" + N(w) + "px;\">";
            d += "<mcimg src=\"mcres:ui_menu_btn" + std::string(off ? "_off" : "") + ".png\" nine=\"3 3 3 3\" style=\"left:0px;top:0px;width:" + N(w) + "px;height:20px;\"></mcimg>";
            if (!off)
                d += "<mcimg class=\"h\" src=\"mcres:ui_menu_btn_hi.png\" nine=\"3 3 3 3\" style=\"left:0px;top:0px;width:" + N(w) + "px;height:20px;\"></mcimg>";
            d += "<div class=\"lbl\" style=\"width:" + N(w) + "px;\">" + EscapeRml(label) + "</div></div>\n";
            return d;
        }

        std::string Head(const std::string& key, int x, int y, int size, const std::string& extra = "") {
            if (key.empty())
                return {};
            return "<mcimg src=\"mchead:" + EscapeRml(key) + "\" style=\"left:" + N(x) + "px;top:" + N(y) + "px;width:" + N(size) + "px;height:" + N(size) + "px;" + extra +
                   "\"></mcimg>";
        }

        // A menu button with the player's head at its left (the "Logged in as" button).
        std::string HeadBtn(const std::string& id, int x, int y, int w, const std::string& label, const std::string& head) {
            std::string d = "<div id=\"" + id + "\" class=\"btn\" style=\"left:" + N(x) + "px;top:" + N(y) + "px;width:" + N(w) + "px;\">";
            d += "<mcimg src=\"mcres:ui_menu_btn.png\" nine=\"3 3 3 3\" style=\"left:0px;top:0px;width:" + N(w) + "px;height:20px;\"></mcimg>";
            d += "<mcimg class=\"h\" src=\"mcres:ui_menu_btn_hi.png\" nine=\"3 3 3 3\" style=\"left:0px;top:0px;width:" + N(w) + "px;height:20px;\"></mcimg>";
            const int textX = head.empty() ? 6 : 20;
            d += Head(head, 4, 4, 12);
            d += "<div class=\"lbl\" style=\"left:" + N(textX) + "px;width:" + N(w - textX - 4) + "px;text-align:left;\">" + EscapeRml(label) + "</div></div>\n";
            return d;
        }

        // A square 20x20 menu button showing an image instead of a label (like Java's language button).
        std::string IconBtn(const std::string& id, int x, int y, const std::string& icon) {
            std::string d = "<div id=\"" + id + "\" class=\"btn\" style=\"left:" + N(x) + "px;top:" + N(y) + "px;width:20px;\">";
            d += "<mcimg src=\"mcres:ui_menu_btn.png\" nine=\"3 3 3 3\" style=\"left:0px;top:0px;width:20px;height:20px;\"></mcimg>";
            d += "<mcimg class=\"h\" src=\"mcres:ui_menu_btn_hi.png\" nine=\"3 3 3 3\" style=\"left:0px;top:0px;width:20px;height:20px;\"></mcimg>";
            d += "<mcimg src=\"" + EscapeRml(icon) + "\" style=\"left:4px;top:4px;width:12px;height:12px;\"></mcimg></div>\n";
            return d;
        }

        std::string Text(int x, int y, int w, const std::string& text, const std::string& cls = "") {
            return "<div class=\"txt " + cls + "\" style=\"left:" + N(x) + "px;top:" + N(y) + "px;width:" + N(w) + "px;height:9px;\">" + EscapeRml(text) + "</div>\n";
        }

        std::string Dirt(int w, int h, const char* shade) {
            return std::string("<mcimg src=\"textures/blocks/dirt\" tiled=\"1\" tilescale=\"2\" color=\"") + shade + "\" style=\"left:0px;top:0px;width:" + N(w) + "px;height:" + N(h) + "px;\"></mcimg>\n";
        }

        std::string Field(const std::string& id, int x, int y, int w, const std::string& value, bool focused, bool caret) {
            std::string d = "<div id=\"" + id + "\" class=\"field\" style=\"left:" + N(x) + "px;top:" + N(y) + "px;width:" + N(w) + "px;\">";
            d += "<mcimg src=\"mcres:ui_menu_field" + std::string(focused ? "_hi" : "") + ".png\" nine=\"1 1 1 1\" style=\"left:0px;top:0px;width:" + N(w) + "px;height:20px;\"></mcimg>";
            d += "<div class=\"txt val\" style=\"width:" + N(w - 8) + "px;\">" + EscapeRml(value + (focused && caret ? "_" : "")) + "</div></div>\n";
            return d;
        }

        // Width of a line of Minecraft font text in GUI pixels (glyph advances of the default font).
        int TextWidth(const std::string& text) {
            int n = 0;
            for (unsigned char c : text) {
                if ((c & 0xC0) == 0x80)
                    continue;
                if (c == 'i' || c == '!' || c == '.' || c == ',' || c == ':' || c == ';' || c == '|' || c == '\'')
                    n += 2;
                else if (c == 'l' || c == '`')
                    n += 3;
                else if (c == 'I' || c == 't' || c == '[' || c == ']' || c == ' ')
                    n += 4;
                else if (c == 'f' || c == 'k' || c == '<' || c == '>' || c == '{' || c == '}' || c == '(' || c == ')' || c == '*' || c == '"')
                    n += 5;
                else if (c == '@' || c == '~')
                    n += 7;
                else
                    n += 6;
            }
            return n;
        }

        // The dark, framed panel the account popup uses (styled after Java's friends menu).
        std::string Panel(int x, int y, int w, int h) {
            std::string d;
            d += "<div style=\"left:" + N(x - 2) + "px;top:" + N(y - 2) + "px;width:" + N(w + 4) + "px;height:" + N(h + 4) + "px;background-color:#000000;z-index:11;\"></div>\n";
            d += "<div style=\"left:" + N(x - 1) + "px;top:" + N(y - 1) + "px;width:" + N(w + 2) + "px;height:" + N(h + 2) + "px;background-color:#e0e0e0;z-index:11;\"></div>\n";
            d += "<div style=\"left:" + N(x + 1) + "px;top:" + N(y + 1) + "px;width:" + N(w - 2) + "px;height:" + N(h - 2) + "px;background-color:#373737;z-index:11;\"></div>\n";
            d += "<div style=\"left:" + N(x + 2) + "px;top:" + N(y + 2) + "px;width:" + N(w - 4) + "px;height:" + N(h - 4) + "px;background-color:#2b2b2b;z-index:11;\"></div>\n";
            return d;
        }

        // Darkens whatever is behind a popup (and keeps clicks off it).
        std::string Shade(int w, int h) {
            return "<div style=\"left:0px;top:0px;width:" + N(w) + "px;height:" + N(h) + "px;background-color:#000000a0;z-index:10;\"></div>\n";
        }

        std::string PanoramaFallback(const TitleModel& m, int w, int h) {
            if (m.nativePanorama)
                return {};
            std::string d;
            const int face = std::max(h * 5 / 4, 64);
            const int top = -(face - h) / 2;
            d += "<div style=\"left:0px;top:0px;width:" + N(w) + "px;height:" + N(h) + "px;overflow:hidden;background-color:#000000;\">\n";
            d += "<div id=\"pano\" style=\"left:0px;top:" + N(top) + "px;width:" + N(face * 5) + "px;height:" + N(face) + "px;animation: 240s linear infinite pano;\">\n";
            for (int i = 0; i < 5; i++)
                d += "<mcimg src=\"textures/ui/panorama_" + N(i % 4) + "\" style=\"left:" + N(i * face) + "px;top:0px;width:" + N(face) + "px;height:" + N(face) + "px;\"></mcimg>\n";
            d += "</div></div>\n";
            return d;
        }

        std::string SmallBtn(const std::string& id, int x, int y, int w, const std::string& label) {
            std::string d = "<div id=\"" + id + "\" class=\"sbtn\" style=\"left:" + N(x) + "px;top:" + N(y) + "px;width:" + N(w) + "px;\">";
            d += "<mcimg src=\"mcres:ui_menu_btn.png\" nine=\"3 3 3 3\" style=\"left:0px;top:0px;width:" + N(w) + "px;height:20px;\"></mcimg>";
            d += "<mcimg class=\"h\" src=\"mcres:ui_menu_btn_hi.png\" nine=\"3 3 3 3\" style=\"left:0px;top:0px;width:" + N(w) + "px;height:20px;\"></mcimg>";
            d += "<div class=\"lbl\" style=\"width:" + N(w) + "px;\">" + EscapeRml(label) + "</div></div>\n";
            return d;
        }

        std::string Accounts(const TitleModel& m, int w, int h) {
            std::string d = PanoramaFallback(m, w, h) + Shade(w, h);
            const int pw = 240;
            const int rowH = 24;
            const int count = static_cast<int>(m.accounts.size());
            const int rows = std::max(1, std::min(count, TitleAccountRows(h)));
            const int scroll = std::clamp(m.accountScroll, 0, std::max(0, count - rows));
            const int ph = 46 + rows * rowH + 56;
            // Room above for the header tab.
            const int px = w / 2 - pw / 2, py = std::max(20, h / 2 - ph / 2 + 8);
            d += Panel(px, py, pw, ph);
            // Header tab, as on the friends menu.
            d += "<div style=\"left:" + N(px + pw / 2 - 40) + "px;top:" + N(py - 16) + "px;width:80px;height:16px;background-color:#e0e0e0;z-index:11;\"></div>\n";
            d += "<div style=\"left:" + N(px + pw / 2 - 39) + "px;top:" + N(py - 15) + "px;width:78px;height:16px;background-color:#2b2b2b;z-index:12;\"></div>\n";
            d += "<div class=\"txt ctr\" style=\"left:" + N(px + pw / 2 - 40) + "px;top:" + N(py - 11) + "px;width:80px;height:9px;z-index:13;\">Accounts</div>\n";
            d += "<div style=\"left:" + N(px + pw / 2 - 22) + "px;top:" + N(py - 1) + "px;width:44px;height:1px;background-color:#ffffff;z-index:13;\"></div>\n";
            // Header: who is logged in, with room to breathe under the tab.
            const int labelX = m.accountHead.empty() ? 10 : 30;
            if (!m.accountHead.empty())
                d += Head(m.accountHead, px + 10, py + 11, 14, "z-index:13;") + "\n";
            d += "<div class=\"txt dim\" style=\"left:" + N(px + labelX) + "px;top:" + N(py + 14) + "px;width:" + N(pw - labelX - 10) + "px;height:9px;z-index:13;\">" +
                 EscapeRml(m.accountLabel) + "</div>\n";
            d += "<div style=\"left:" + N(px + 2) + "px;top:" + N(py + 35) + "px;width:" + N(pw - 4) + "px;height:1px;background-color:#555555;z-index:12;\"></div>\n";
            int y = py + 41;
            if (m.accounts.empty())
                d += "<div class=\"txt ctr gray\" style=\"left:" + N(px) + "px;top:" + N(y + 6) + "px;width:" + N(pw) + "px;height:9px;z-index:13;\">No accounts yet</div>\n";
            if (count > rows) {
                // Scrollbar along the right edge of the list.
                const int trackH = rows * rowH - 2;
                const int thumbH = std::max(8, trackH * rows / count);
                const int thumbY = (trackH - thumbH) * scroll / std::max(1, count - rows);
                d += "<div style=\"left:" + N(px + pw - 6) + "px;top:" + N(y) + "px;width:2px;height:" + N(trackH) + "px;background-color:#1a1a1a;z-index:13;\"></div>\n";
                d += "<div style=\"left:" + N(px + pw - 6) + "px;top:" + N(y + thumbY) + "px;width:2px;height:" + N(thumbH) + "px;background-color:#a0a0a0;z-index:13;\"></div>\n";
            }
            for (int i = scroll; i < count && i < scroll + rows; i++, y += rowH) {
                const AccountRow& a = m.accounts[static_cast<size_t>(i)];
                d += "<div id=\"" + std::string(kAccountRowPrefix) + N(i) + "\" class=\"acc\" style=\"left:" + N(px + 6) + "px;top:" + N(y) + "px;width:" + N(pw - 36) +
                     "px;height:" + N(rowH - 2) + "px;\">";
                d += "<div class=\"hl\" style=\"left:0px;top:0px;width:" + N(pw - 36) + "px;height:" + N(rowH - 2) + "px;background-color:#ffffff20;\"></div>";
                if (a.active)
                    d += "<div style=\"left:0px;top:0px;width:2px;height:" + N(rowH - 2) + "px;background-color:#55ff55;\"></div>";
                // Avatar: the head of the skin last used with the account (a lettered square until
                // the skin is known).
                if (!a.headKey.empty()) {
                    d += Head(a.headKey, 5, 3, 16);
                } else {
                    const std::string initial = a.name.empty() ? "?" : std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(a.name[0]))));
                    d += "<div style=\"left:5px;top:3px;width:16px;height:16px;background-color:" + std::string(a.microsoft ? "#3c8527" : "#5a5a5a") + ";\"></div>";
                    d += "<div class=\"txt ctr\" style=\"left:5px;top:7px;width:16px;height:9px;\">" + EscapeRml(initial) + "</div>";
                }
                // The exact username, then what kind of account it is.
                d += "<div class=\"txt\" style=\"left:26px;top:2px;width:" + N(pw - 70) + "px;height:9px;overflow:hidden;\">" +
                     EscapeRml(a.name.empty() ? (a.microsoft ? "(gamertag not known yet)" : "?") : a.name) + "</div>";
                const std::string kind = std::string(a.microsoft ? "Microsoft account" : "Offline account") + (a.active ? " - in use" : "");
                d += "<div class=\"txt " + std::string(a.active ? "green" : "gray") + "\" style=\"left:26px;top:12px;width:" + N(pw - 70) + "px;height:9px;\">" +
                     EscapeRml(kind) + "</div>";
                d += "</div>\n";
                d += SmallBtn(std::string(kAccountRemovePrefix) + N(i), px + pw - 28, y + 1, 20, "x");
            }
            const int by = py + ph - 50;
            d += "<div style=\"left:" + N(px + 2) + "px;top:" + N(by - 6) + "px;width:" + N(pw - 4) + "px;height:1px;background-color:#555555;z-index:12;\"></div>\n";
            d += SmallBtn(kAccountAddMicrosoft, px + 8, by, (pw - 20) / 2, "Add Microsoft");
            d += SmallBtn(kAccountAddOffline, px + 12 + (pw - 20) / 2, by, (pw - 20) / 2, "Add Offline");
            d += SmallBtn(kAccountDone, px + 8, by + 24, pw - 16, "Done");
            return d;
        }

        std::string AccountName(const TitleModel& m, int w, int h) {
            std::string d = PanoramaFallback(m, w, h) + Shade(w, h);
            const int pw = 240, ph = 104;
            const int px = w / 2 - pw / 2, py = std::max(8, h / 2 - ph / 2);
            d += Panel(px, py, pw, ph);
            d += "<div class=\"txt ctr\" style=\"left:" + N(px) + "px;top:" + N(py + 10) + "px;width:" + N(pw) + "px;height:9px;z-index:13;\">Add Offline Account</div>\n";
            d += "<div class=\"txt dim\" style=\"left:" + N(px + 12) + "px;top:" + N(py + 28) + "px;width:" + N(pw - 24) + "px;height:9px;z-index:13;\">Username</div>\n";
            std::string field = Field(kAccountNameField, px + 12, py + 40, pw - 24, m.accountName, true, m.caret);
            d += "<div style=\"left:0px;top:0px;z-index:14;\">" + field + "</div>"; // above the shade
            d += SmallBtn(kAccountNameDone, px + 8, py + ph - 28, (pw - 20) / 2, "Add");
            d += SmallBtn(kAccountCancel, px + 12 + (pw - 20) / 2, py + ph - 28, (pw - 20) / 2, "Cancel");
            return d;
        }

        std::string AccountSignIn(const TitleModel& m, int w, int h) {
            std::string d = PanoramaFallback(m, w, h) + Shade(w, h);
            const int pw = 260, ph = 120;
            const int px = w / 2 - pw / 2, py = std::max(8, h / 2 - ph / 2);
            d += Panel(px, py, pw, ph);
            const auto line = [&](int y, const std::string& text, const std::string& cls) {
                d += "<div class=\"txt ctr " + cls + "\" style=\"left:" + N(px) + "px;top:" + N(py + y) + "px;width:" + N(pw) + "px;height:9px;z-index:13;\">" + EscapeRml(text) + "</div>\n";
            };
            line(10, "Sign in with Microsoft", "");
            if (m.signInCode.empty()) {
                line(40, m.signInStatus.empty() ? "Contacting Microsoft..." : m.signInStatus, "gray");
            } else {
                line(30, "Open this page in your browser:", "dim");
                line(42, m.signInUrl, "yellow");
                line(58, "and enter the code:", "dim");
                line(70, m.signInCode, "green");
                if (!m.signInStatus.empty())
                    line(84, m.signInStatus, "gray");
            }
            d += SmallBtn(kAccountCancel, px + pw / 2 - 50, py + ph - 26, 100, "Cancel");
            return d;
        }

        std::string Main(const TitleModel& m, int w, int h) {
            std::string d;
            const int face = std::max(h * 5 / 4, 64);
            const int top = -(face - h) / 2;
            if (!m.nativePanorama) {
                d += "<div style=\"left:0px;top:0px;width:" + N(w) + "px;height:" + N(h) + "px;overflow:hidden;background-color:#000000;\">\n";
                d += "<div id=\"pano\" style=\"left:0px;top:" + N(top) + "px;width:" + N(face * 5) + "px;height:" + N(face) + "px;animation: 240s linear infinite pano;\">\n";
                for (int i = 0; i < 5; i++)
                    d += "<mcimg src=\"textures/ui/panorama_" + N(i % 4) + "\" style=\"left:" + N(i * face) + "px;top:0px;width:" + N(face) + "px;height:" + N(face) + "px;\"></mcimg>\n";
                d += "</div></div>\n";
            }
            const int cx = w / 2;
            if (!m.logoSrc.empty()) {
                // Centred in the 256x64 slot the Java logo uses, so the buttons below don't move.
                const int lw = std::clamp(m.logoW, 1, 256), lh = std::clamp(m.logoH, 1, 64);
                d += "<mcimg src=\"" + EscapeRml(m.logoSrc) + "\" style=\"left:" + N(cx - lw / 2) + "px;top:" + N(30 + (64 - lh) / 2) + "px;width:" + N(lw) +
                     "px;height:" + N(lh) + "px;z-index:3;\"></mcimg>\n";
            } else {
                d += "<mcimg src=\"mcres:ui_menu_logo.png\" style=\"left:" + N(cx - 128) + "px;top:30px;width:256px;height:64px;z-index:3;\"></mcimg>\n";
            }
            // Java's layout with a third full-width row (where Realms sits): Mods, with the small
            // Wardrobe button beside it.
            const int j = h / 4 + 48;
            d += Btn("", cx - 100, j, 200, "Singleplayer", true);
            d += Btn(kTitleMulti, cx - 100, j + 24, 200, "Multiplayer");
            d += Btn(kTitleMods, cx - 100, j + 48, 200, "Mods");
            d += IconBtn(kTitleWardrobe, cx + 104, j + 48, "textures/ui/glyph_skin_pack.png"); // the pause screen's hanger
            d += Btn(kTitleOptions, cx - 100, j + 84, 98, "Options...");
            d += Btn(kTitleQuit, cx + 2, j + 84, 98, "Quit Game");
            if (!m.accountUser.empty()) {
                // Head and username, snug (above the logo, so a long name may take the width it needs).
                const int textX = m.accountHead.empty() ? 6 : 20;
                const int bw = std::clamp(textX + TextWidth(m.accountUser) + 7, 40, std::max(40, w - 8));
                d += HeadBtn(kTitleAccounts, 4, 4, bw, m.accountUser, m.accountHead);
            }
            d += Text(2, h - 10, w / 2, m.version);
            d += "<div class=\"txt\" style=\"left:" + N(w / 2) + "px;top:" + N(h - 10) + "px;width:" + N(w / 2 - 1) + "px;height:9px;text-align:right;\">" + EscapeRml(m.copyright) + "</div>\n";
            return d;
        }

        std::string Servers(const TitleModel& m, int w, int h) {
            std::string d = Dirt(w, h, "0.25 0.25 0.25");
            const int cx = w / 2;
            const int listTop = TitleListTop(), listBottom = TitleListBottom(h);
            d += "<mcimg src=\"textures/blocks/dirt\" tiled=\"1\" tilescale=\"2\" color=\"0.12 0.12 0.12\" style=\"left:0px;top:" + N(listTop) + "px;width:" + N(w) + "px;height:" + N(listBottom - listTop) + "px;z-index:1;\"></mcimg>\n";
            d += "<div style=\"left:0px;top:" + N(listTop) + "px;width:" + N(w) + "px;height:4px;decorator:vertical-gradient(#000000ff #00000000);z-index:2;\"></div>\n";
            d += "<div style=\"left:0px;top:" + N(listBottom - 4) + "px;width:" + N(w) + "px;height:4px;decorator:vertical-gradient(#00000000 #000000ff);z-index:2;\"></div>\n";
            d += "<div style=\"left:0px;top:" + N(listTop - 1) + "px;width:" + N(w) + "px;height:1px;background-color:#000000;z-index:3;\"></div>\n";
            d += "<div style=\"left:0px;top:" + N(listBottom) + "px;width:" + N(w) + "px;height:1px;background-color:#000000;z-index:3;\"></div>\n";
            {
                // Shown left to right: LAN, Friends, Realms, Servers (the ids are the ServerTab values).
                static const struct {
                    ServerTab tab;
                    const char* label;
                } kTabs[] = {{ServerTab::Lan, "LAN"}, {ServerTab::Friends, "Friends"}, {ServerTab::Realms, "Realms"}, {ServerTab::Saved, "Servers"}};
                const int tabW = 70, gap = 2;
                const int x0 = cx - (4 * tabW + 3 * gap) / 2;
                for (int t = 0; t < 4; t++)
                    d += Btn(std::string(kServerTabPrefix) + N(static_cast<int>(kTabs[t].tab)), x0 + t * (tabW + gap), 6, tabW, kTabs[t].label, m.tab == kTabs[t].tab);
            }
            const std::vector<ServerEntry>& list = m.Shown();
            const int rows = TitleVisibleRows(h);
            const int rowX = cx - kRowW / 2;
            int y = listTop + 4;
            for (int i = m.scroll; i < static_cast<int>(list.size()) && i < m.scroll + rows; i++, y += kRowH) {
                const ServerEntry& s = list[static_cast<size_t>(i)];
                if (i == m.selected) {
                    d += "<div style=\"left:" + N(rowX - 2) + "px;top:" + N(y - 2) + "px;width:" + N(kRowW + 4) + "px;height:" + N(kRowH + 0) + "px;background-color:#ffffff;z-index:2;\"></div>\n";
                    d += "<div style=\"left:" + N(rowX - 1) + "px;top:" + N(y - 1) + "px;width:" + N(kRowW + 2) + "px;height:" + N(kRowH - 2) + "px;background-color:#000000;z-index:2;\"></div>\n";
                }
                if (s.kind == ServerEntry::Kind::Header) {
                    const bool open = m.friendsOpen[std::clamp<long long>(s.realmId, 0, 2)];
                    d += "<div id=\"" + std::string(kServerRowPrefix) + N(i) + "\" class=\"row\" style=\"left:" + N(rowX) + "px;top:" + N(y) + "px;width:" + N(kRowW) + "px;height:" + N(kRowH - 4) + "px;\">\n";
                    d += "<mcimg src=\"" + std::string(open ? "textures/ui/chevron_white_down.png" : "textures/ui/chevron_white_up.png") + "\" style=\"left:4px;top:11px;width:8px;height:8px;\"></mcimg>\n";
                    d += Text(18, 11, kRowW - 20, s.name, s.realmId == 2 ? "gray" : "green");
                    d += "</div>\n";
                    continue;
                }
                d += "<div id=\"" + std::string(kServerRowPrefix) + N(i) + "\" class=\"row\" style=\"left:" + N(rowX) + "px;top:" + N(y) + "px;width:" + N(kRowW) + "px;height:" + N(kRowH - 4) + "px;\">\n";
                d += "<div style=\"left:0px;top:0px;width:32px;height:32px;background-color:#3a2f26;\"></div>\n";
                if (!s.icon.empty())
                    d += "<mcimg src=\"mchead:" + EscapeRml(s.icon) + "\" style=\"left:0px;top:0px;width:32px;height:32px;" + (s.offline ? "opacity:0.5;" : "") + "\"></mcimg>\n";
                if (Joinable(s))
                d += "<div id=\"" + std::string(kServerJoinPrefix) + N(i) + "\" class=\"go\" style=\"left:0px;top:0px;width:32px;height:32px;\"><mcimg src=\"mcres:ui_menu_join_hi.png\" style=\"left:0px;top:0px;width:32px;height:32px;\"></mcimg></div>\n";
                d += Text(35, 1, kRowW - 110, s.name.empty() ? "Minecraft Server" : s.name);
                // Live status (from the ping): players and latency top right, MOTD below the name.
                std::string right, rightCls = "gray";
                using St = ServerEntry::Status;
                if (s.kind != ServerEntry::Kind::Server) {
                    d += "<div class=\"txt " + std::string(s.offline ? "dark" : "green") + "\" style=\"left:" + N(kRowW - 105) + "px;top:1px;width:100px;height:9px;text-align:right;\">" +
                         EscapeRml(s.right) + "</div>\n";
                    d += Text(35, 12, kRowW - 40, s.motd.empty() ? " " : s.motd, s.offline ? "dark" : "gray");
                    d += Text(35, 23, kRowW - 40, s.note, "dark");
                    d += "</div>\n";
                    continue;
                }
                if (s.status == St::Pinging || s.status == St::Unknown) {
                    static const char* const kDots[] = {"Pinging", "Pinging.", "Pinging..", "Pinging..."};
                    right = kDots[m.spinner & 3];
                    rightCls = "dark";
                } else if (s.status == St::Online) {
                    right = N(s.players) + "/" + N(s.maxPlayers) + "  ";
                } else {
                    right = "No connection";
                    rightCls = "red";
                }
                d += "<div class=\"txt " + rightCls + "\" style=\"left:" + N(kRowW - 105) + "px;top:1px;width:" + N(s.status == St::Online ? 72 : 100) +
                     "px;height:9px;text-align:right;\">" + EscapeRml(right) + "</div>\n";
                if (s.status == St::Online) {
                    const char* latencyCls = s.pingMs < 150 ? "green" : (s.pingMs < 300 ? "yellow" : "red");
                    d += "<div class=\"txt " + std::string(latencyCls) + "\" style=\"left:" + N(kRowW - 33) + "px;top:1px;width:30px;height:9px;text-align:right;\">" +
                         N(s.pingMs) + "ms</div>\n";
                    d += Text(35, 12, kRowW - 40, s.motd.empty() ? "A Minecraft Server" : s.motd, "gray");
                    d += Text(35, 23, kRowW - 40, s.address + (s.version.empty() ? "" : "  -  " + s.version), "dark");
                } else if (s.status == St::Offline) {
                    d += Text(35, 12, kRowW - 40, "Can't connect to server", "red");
                    d += Text(35, 23, kRowW - 40, s.address, "dark");
                } else {
                    d += Text(35, 12, kRowW - 40, s.address, "gray");
                }
                d += "</div>\n";
            }
            if (list.empty() && m.tab != ServerTab::Saved) {
                d += "<div class=\"txt ctr\" style=\"left:0px;top:" + N(listTop + 28) + "px;width:" + N(w) + "px;height:9px;\">" + EscapeRml(m.tabMessage) + "</div>\n";
            } else if (list.empty()) {
                d += "<div class=\"txt ctr\" style=\"left:0px;top:" + N(listTop + 28) + "px;width:" + N(w) + "px;height:9px;\">Scanning for games on your local network</div>\n";
                static const char* const kSpin[] = {"O o o", "o O o", "o o O", "o O o"};
                d += "<div class=\"txt ctr gray\" style=\"left:0px;top:" + N(listTop + 40) + "px;width:" + N(w) + "px;height:9px;\">" + std::string(kSpin[m.spinner & 3]) + "</div>\n";
            }
            if (!m.joinMessage.empty())
                d += "<div class=\"txt ctr yellow\" style=\"left:0px;top:" + N(h - 62) + "px;width:" + N(w) + "px;height:9px;z-index:4;\">" + EscapeRml(m.joinMessage) + "</div>\n";
            const bool has = m.selected >= 0 && m.selected < static_cast<int>(list.size()) && Joinable(list[static_cast<size_t>(m.selected)]);
            const bool hasRow = m.selected >= 0 && m.selected < static_cast<int>(list.size());
            const bool saved = m.tab == ServerTab::Saved;
            const int r1 = h - 52, r2 = h - 28;
            d += Btn(has ? kServerJoin : "", cx - 154, r1, 100, m.tab == ServerTab::Friends ? "Join Friend" : (m.tab == ServerTab::Realms ? "Join Realm" : "Join Server"), !has);
            d += Btn(kServerDirect, cx - 50, r1, 100, "Direct Connection");
            if (m.tab == ServerTab::Realms)
                d += Btn(kServerAddRealm, cx + 54, r1, 100, "Add Realm");
            else
                d += Btn(saved ? kServerAdd : "", cx + 54, r1, 100, "Add Server", !saved);
            d += Btn(hasRow && saved ? kServerEdit : "", cx - 154, r2, 72, "Edit", !(hasRow && saved));
            d += Btn(hasRow && saved ? kServerDelete : "", cx - 74, r2, 72, "Delete", !(hasRow && saved));
            d += Btn(kServerRefresh, cx + 4, r2, 72, "Refresh");
            d += Btn(kServerCancel, cx + 82, r2, 72, "Cancel");
            return d;
        }

        std::string Edit(const TitleModel& m, int w, int h) {
            std::string d = Dirt(w, h, "0.25 0.25 0.25");
            const int cx = w / 2;
            d += "<div class=\"txt ctr\" style=\"left:0px;top:17px;width:" + N(w) + "px;height:9px;\">" + (m.adding ? "Add Server" : "Edit Server Info") + "</div>\n";
            d += Text(cx - 100, 53, 200, "Server Name", "dim");
            d += Field(kEditName, cx - 100, 66, 200, m.name, m.focus == 0, m.caret);
            d += Text(cx - 100, 94, 200, "Server Address", "dim");
            d += Field(kEditAddress, cx - 100, 106, 200, m.address, m.focus == 1, m.caret);
            d += Btn(kEditDone, cx - 100, h / 4 + 96 + 18, 200, "Done");
            d += Btn(kEditCancel, cx - 100, h / 4 + 120 + 18, 200, "Cancel");
            return d;
        }

        std::string Direct(const TitleModel& m, int w, int h) {
            std::string d = Dirt(w, h, "0.25 0.25 0.25");
            const int cx = w / 2;
            d += "<div class=\"txt ctr\" style=\"left:0px;top:20px;width:" + N(w) + "px;height:9px;\">Direct Connection</div>\n";
            d += Text(cx - 100, 94, 200, "Server Address", "dim");
            d += Field(kEditAddress, cx - 100, 106, 200, m.address, true, m.caret);
            d += Btn(kEditDone, cx - 100, h / 4 + 96 + 12, 200, "Join Server");
            d += Btn(kEditCancel, cx - 100, h / 4 + 120 + 12, 200, "Cancel");
            return d;
        }
    }

    namespace {
        std::string AddRealm(const TitleModel& m, int w, int h) {
            std::string d = Dirt(w, h, "0.25 0.25 0.25");
            const int cx = w / 2;
            d += "<div class=\"txt ctr\" style=\"left:0px;top:20px;width:" + N(w) + "px;height:9px;\">Add Realm</div>\n";
            d += Text(cx - 100, 94, 200, "Realm invite code or link", "dim");
            d += Field(kEditAddress, cx - 100, 106, 200, m.address, true, m.caret);
            if (!m.joinMessage.empty())
                d += "<div class=\"txt ctr yellow\" style=\"left:0px;top:132px;width:" + N(w) + "px;height:9px;\">" + EscapeRml(m.joinMessage) + "</div>\n";
            d += Btn(kEditDone, cx - 100, h / 4 + 96 + 12, 200, "Add Realm");
            d += Btn(kEditCancel, cx - 100, h / 4 + 120 + 12, 200, "Cancel");
            return d;
        }
    }

    int TitleRowHeight() { return kRowH; }
    int TitleListTop() { return 32; }
    int TitleListBottom(int guiH) { return guiH - 64; }
    int TitleVisibleRows(int guiH) { return std::max(1, (TitleListBottom(guiH) - TitleListTop() - 4) / kRowH); }

    std::string BuildTitleRml(const TitleModel& m, int guiW, int guiH) {
        std::string out = kStyle;
        const std::string width = std::to_string(guiW), height = std::to_string(guiH);
        const std::string span = std::to_string(std::max(guiH * 5 / 4, 64) * 4);
        const std::pair<const char*, const std::string*> keys[] = {{"{{w}}", &width}, {"{{h}}", &height}, {"{{span}}", &span}};
        for (const auto& [key, value] : keys) {
            const size_t at = out.find(key);
            out.replace(at, std::char_traits<char>::length(key), *value);
        }
        switch (m.page) {
            case TitlePage::Main: out += Main(m, guiW, guiH); break;
            case TitlePage::Servers: out += Servers(m, guiW, guiH); break;
            case TitlePage::Edit: out += Edit(m, guiW, guiH); break;
            case TitlePage::Direct: out += Direct(m, guiW, guiH); break;
            case TitlePage::AddRealm: out += AddRealm(m, guiW, guiH); break;
            case TitlePage::Accounts: out += Accounts(m, guiW, guiH); break;
            case TitlePage::AccountName: out += AccountName(m, guiW, guiH); break;
            case TitlePage::AccountSignIn: out += AccountSignIn(m, guiW, guiH); break;
        }
        out += "</body>\n</rml>\n";
        return out;
    }

    int TitleAccountRows(int guiH) {
        // Popup = 102 px of header and buttons plus the rows, with 20 px above for the tab.
        return std::max(1, (guiH - 20 - 6 - 102) / 24);
    }
}
