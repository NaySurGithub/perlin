#include "client/ui/TitleScreen.hpp"

#include "client/ui/RmlEmit.hpp"

#include <algorithm>
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

        std::string Main(const TitleModel& m, int w, int h) {
            std::string d;
            const int face = std::max(h * 5 / 4, 64);
            const int top = -(face - h) / 2;
            d += "<div style=\"left:0px;top:0px;width:" + N(w) + "px;height:" + N(h) + "px;overflow:hidden;background-color:#000000;\">\n";
            d += "<div id=\"pano\" style=\"left:0px;top:" + N(top) + "px;width:" + N(face * 5) + "px;height:" + N(face) + "px;animation: 240s linear infinite pano;\">\n";
            for (int i = 0; i < 5; i++)
                d += "<mcimg src=\"textures/ui/panorama_" + N(i % 4) + "\" style=\"left:" + N(i * face) + "px;top:0px;width:" + N(face) + "px;height:" + N(face) + "px;\"></mcimg>\n";
            d += "</div></div>\n";
            const int cx = w / 2;
            d += "<mcimg src=\"mcres:ui_menu_logo.png\" style=\"left:" + N(cx - 128) + "px;top:30px;width:256px;height:64px;z-index:3;\"></mcimg>\n";
            d += "<mcimg src=\"mcres:ui_menu_edition.png\" style=\"left:" + N(cx - 64) + "px;top:67px;width:128px;height:16px;z-index:4;\"></mcimg>\n";
            const int j = h / 4 + 48;
            d += Btn("", cx - 100, j, 200, "Singleplayer", true);
            d += Btn(kTitleMulti, cx - 100, j + 24, 200, "Multiplayer");
            d += Btn(kTitleQuit, cx - 100, j + 60, 200, "Quit Game");
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
            d += "<div class=\"txt ctr\" style=\"left:0px;top:12px;width:" + N(w) + "px;height:9px;\">Play Multiplayer</div>\n";
            const int rows = TitleVisibleRows(h);
            const int rowX = cx - kRowW / 2;
            int y = listTop + 4;
            for (int i = m.scroll; i < static_cast<int>(m.servers.size()) && i < m.scroll + rows; i++, y += kRowH) {
                const ServerEntry& s = m.servers[static_cast<size_t>(i)];
                if (i == m.selected) {
                    d += "<div style=\"left:" + N(rowX - 2) + "px;top:" + N(y - 2) + "px;width:" + N(kRowW + 4) + "px;height:" + N(kRowH + 0) + "px;background-color:#ffffff;z-index:2;\"></div>\n";
                    d += "<div style=\"left:" + N(rowX - 1) + "px;top:" + N(y - 1) + "px;width:" + N(kRowW + 2) + "px;height:" + N(kRowH - 2) + "px;background-color:#000000;z-index:2;\"></div>\n";
                }
                d += "<div id=\"" + std::string(kServerRowPrefix) + N(i) + "\" class=\"row\" style=\"left:" + N(rowX) + "px;top:" + N(y) + "px;width:" + N(kRowW) + "px;height:" + N(kRowH - 4) + "px;\">\n";
                d += "<div style=\"left:0px;top:0px;width:32px;height:32px;background-color:#3a2f26;\"></div>\n";
                d += "<div id=\"" + std::string(kServerJoinPrefix) + N(i) + "\" class=\"go\" style=\"left:0px;top:0px;width:32px;height:32px;\"><mcimg src=\"mcres:ui_menu_join_hi.png\" style=\"left:0px;top:0px;width:32px;height:32px;\"></mcimg></div>\n";
                d += Text(35, 1, kRowW - 40, s.name.empty() ? "Minecraft Server" : s.name);
                d += Text(35, 12, kRowW - 40, s.address, "gray");
                d += "</div>\n";
            }
            if (m.servers.empty()) {
                d += "<div class=\"txt ctr\" style=\"left:0px;top:" + N(listTop + 28) + "px;width:" + N(w) + "px;height:9px;\">Scanning for games on your local network</div>\n";
                static const char* const kSpin[] = {"O o o", "o O o", "o o O", "o O o"};
                d += "<div class=\"txt ctr gray\" style=\"left:0px;top:" + N(listTop + 40) + "px;width:" + N(w) + "px;height:9px;\">" + std::string(kSpin[m.spinner & 3]) + "</div>\n";
            }
            const bool has = m.selected >= 0 && m.selected < static_cast<int>(m.servers.size());
            const int r1 = h - 52, r2 = h - 28;
            d += Btn(has ? kServerJoin : "", cx - 154, r1, 100, "Join Server", !has);
            d += Btn(kServerDirect, cx - 50, r1, 100, "Direct Connection");
            d += Btn(kServerAdd, cx + 54, r1, 100, "Add Server");
            d += Btn(has ? kServerEdit : "", cx - 154, r2, 72, "Edit", !has);
            d += Btn(has ? kServerDelete : "", cx - 74, r2, 72, "Delete", !has);
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
        }
        out += "</body>\n</rml>\n";
        return out;
    }
}
