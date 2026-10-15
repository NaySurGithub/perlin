#include "client/ui/InventoryScreen.hpp"

#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiTemplate.hpp"
#include "client/i18n/Lang.hpp"

#include <algorithm>
#include <cctype>
#include <vector>
#include <cstdlib>
#include <cstdio>

namespace perlin::ui {
    std::string DefaultInventoryScreenTemplate() {
        return R"PERLINRML(<!--perlin-ui:3-->
<rml>
<head>
<title>inventory</title>
<style>
/* Edit this file while the game runs: it is reloaded when it changes. {{name}} placeholders are filled in by Perlin. */
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }
div { display: block; position: absolute; }
mcitem { display: block; position: absolute; }
#dim { left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; background-color: rgba(16,16,16,200); }
#panel { left: {{x}}px; top: {{y}}px; width: 176px; height: 166px; }
.r { position: absolute; }
.label { font-family: minecraft; font-size: 8px; line-height: 9px; color: #404040; white-space: nowrap; }
.slot { width: 18px; height: 18px; z-index: 2; }
.slot .hlb { left: 1px; top: 1px; width: 16px; height: 16px; background-color: transparent; z-index: 1; }
.slot .hl { left: 1px; top: 1px; width: 16px; height: 16px; background-color: transparent; z-index: 6; }
.slot:hover .hlb { background-color: rgba(255,255,255,96); }
.slot:hover .hl { background-color: rgba(255,255,255,32); }
.slot mcitem { z-index: 4; }
.cnt { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; color: #ffffff; text-align: right; white-space: nowrap; z-index: 5; }
#cursor { z-index: 30; pointer-events: none; width: 16px; height: 16px; }
#cursor .cnt { z-index: 31; }
#tip { z-index: 40; background-color: rgba(16,0,16,240); padding: 3px 4px 2px 4px; pointer-events: none; }
#tip .edge { left: -1px; top: 0px; right: -1px; bottom: 0px; background-color: #240056; }
#tip .txt { position: relative; font-family: minecraft-shadow; font-size: 8px; line-height: 10px; color: #ffffff; white-space: nowrap; }
</style>
</head>
<body>
  <div id="dim"></div>
  <div id="panel">{{chrome}}</div>
  <div class="label" style="left: {{lx}}px; top: {{ly}}px; z-index: 3;">{{title}}</div>
  {{slots}}
  {{cursor}}
  {{tooltip}}
</body>
</rml>
)PERLINRML";
    }

    std::string PrettyItemName(const std::string& id) {
        std::string name = id;
        if (const size_t colon = name.find(':'); colon != std::string::npos)
            name = name.substr(colon + 1);
        bool start = true;
        for (char& c : name) {
            if (c == '_') {
                c = ' ';
                start = true;
            } else {
                if (start)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                start = false;
            }
        }
        return name;
    }

    int InventoryAddrFromId(const std::string& id) {
        if (id == "off")
            return kInvAddrOffhand;
        if (id == "co")
            return kInvAddrOutput;
        if (id.size() < 2 || !std::isdigit(static_cast<unsigned char>(id[1])))
            return -1;
        const int v = std::atoi(id.c_str() + 1);
        if (id[0] == 's' && v >= 0 && v < 36)
            return v;
        if (id[0] == 'a' && v >= 0 && v < 4)
            return kInvAddrArmor + v;
        if (id[0] == 'c' && v >= 0 && v < 4)
            return kInvAddrCraft + v;
        return -1;
    }

    void InventorySlotPos(int addr, int& x, int& y) {
        x = y = 0;
        if (addr < 9) { x = 7 + addr * 18; y = 141; }
        else if (addr < 36) { x = 7 + ((addr - 9) % 9) * 18; y = 83 + ((addr - 9) / 9) * 18; }
        else if (addr < 40) { x = 7; y = 7 + (addr - 36) * 18; }
        else if (addr == kInvAddrOffhand) { x = 76; y = 61; }
        else if (addr < kInvAddrOutput) { x = 97 + ((addr - kInvAddrCraft) % 2) * 18; y = 17 + ((addr - kInvAddrCraft) / 2) * 18; }
        else { x = 153; y = 27; }
    }

    namespace {
        std::string Chrome() {
            constexpr int kW = 176, kH = 166;
            std::vector<uint32_t> grid(static_cast<size_t>(kW) * kH, 0);
            const auto parse = [](const char* c) { return static_cast<uint32_t>(std::strtoul(c + 1, nullptr, 16)) | 0x1000000u; };
            const auto r = [&](int x, int y, int w, int h, const char* c) {
                const uint32_t v = parse(c);
                for (int yy = std::max(0, y); yy < std::min(kH, y + h); yy++)
                    for (int xx = std::max(0, x); xx < std::min(kW, x + w); xx++)
                        grid[static_cast<size_t>(yy) * kW + static_cast<size_t>(xx)] = v;
            };
            r(0, 0, 176, 166, "#000000");
            r(1, 1, 174, 164, "#c6c6c6");
            r(1, 1, 172, 2, "#ffffff");
            r(1, 1, 2, 162, "#ffffff");
            r(3, 163, 172, 2, "#555555");
            r(173, 3, 2, 161, "#555555");
            r(1, 1, 1, 1, "#000000"); r(173, 1, 1, 1, "#000000"); r(174, 2, 1, 1, "#000000"); r(3, 3, 1, 1, "#ffffff");
            r(172, 162, 1, 1, "#555555"); r(1, 163, 1, 1, "#000000"); r(2, 164, 1, 1, "#000000"); r(174, 164, 1, 1, "#000000");
            const auto slot = [&](int x, int y) {
                r(x, y, 18, 18, "#ffffff");
                r(x, y, 17, 17, "#373737");
                r(x + 1, y + 1, 16, 16, "#8b8b8b");
                r(x + 17, y, 1, 1, "#8b8b8b");
                r(x, y + 17, 1, 1, "#8b8b8b");
            };
            for (int a = 0; a < 36; a++) {
                int x, y;
                InventorySlotPos(a, x, y);
                slot(x, y);
            }
            for (int a = kInvAddrArmor; a <= kInvAddrOutput; a++) {
                int x, y;
                InventorySlotPos(a, x, y);
                slot(x, y);
            }
            r(25, 7, 51, 72, "#ffffff"); r(25, 7, 50, 71, "#373737"); r(26, 8, 49, 70, "#000000");
            r(75, 7, 1, 1, "#8b8b8b"); r(25, 78, 1, 1, "#8b8b8b");
            r(135, 34, 9, 3, "#8b8b8b");
            static const int head[7][2] = {{29, 13}, {30, 11}, {31, 9}, {32, 7}, {33, 5}, {34, 3}, {35, 1}};
            for (int i = 0; i < 7; i++)
                r(144 + i, head[i][0], 1, head[i][1], "#8b8b8b");

            std::string s;
            std::vector<uint8_t> used(grid.size(), 0);
            char color[16];
            for (int y = 0; y < kH; y++)
                for (int x = 0; x < kW; x++) {
                    const size_t at = static_cast<size_t>(y) * kW + static_cast<size_t>(x);
                    if (used[at] || grid[at] == 0)
                        continue;
                    const uint32_t v = grid[at];
                    int w = 1;
                    while (x + w < kW && !used[at + static_cast<size_t>(w)] && grid[at + static_cast<size_t>(w)] == v)
                        w++;
                    int h = 1;
                    for (; y + h < kH; h++) {
                        bool same = true;
                        for (int k = 0; k < w && same; k++) {
                            const size_t q = static_cast<size_t>(y + h) * kW + static_cast<size_t>(x + k);
                            same = !used[q] && grid[q] == v;
                        }
                        if (!same)
                            break;
                    }
                    for (int yy = 0; yy < h; yy++)
                        for (int xx = 0; xx < w; xx++)
                            used[static_cast<size_t>(y + yy) * kW + static_cast<size_t>(x + xx)] = 1;
                    std::snprintf(color, sizeof(color), "#%06x", v & 0xFFFFFFu);
                    s += "<div class=\"r\" style=\"left:" + std::to_string(x) + "px;top:" + std::to_string(y) + "px;width:" + std::to_string(w) +
                         "px;height:" + std::to_string(h) + "px;background-color:" + color + ";\"></div>";
                }
            return s;
        }

        std::string Cell(const std::string& id, int px, int py, int addr, const HudItem& item) {
            int x, y;
            InventorySlotPos(addr, x, y);
            std::string s = "  <div id=\"" + id + "\" class=\"slot\" style=\"left:" + std::to_string(px + x) + "px;top:" + std::to_string(py + y) +
                            "px;\"><div class=\"hlb\"></div>";
            if (!item.Empty()) {
                s += "<mcitem key=\"" + EscapeRml(item.Key()) + "\" style=\"left:1px;top:1px;width:16px;height:16px;\"";
                if (item.enchanted)
                    s += " glint=\"1\"";
                s += "/>";
                if (item.count > 1)
                    s += "<div class=\"cnt\" style=\"left:-1px;top:10px;width:19px;height:9px;\">" + std::to_string(item.count) + "</div>";
            }
            s += "<div class=\"hl\"></div></div>\n";
            return s;
        }
    }

    std::string BuildInventoryScreenRml(const std::string& templateText, const InventoryView& v, int guiW, int guiH) {
        const int px = (guiW - kInvPanelW) / 2, py = (guiH - kInvPanelH) / 2;
        std::string slots;
        for (int i = 0; i < 4; i++)
            slots += Cell("a" + std::to_string(i), px, py, kInvAddrArmor + i, v.armor[static_cast<size_t>(i)]);
        slots += Cell("off", px, py, kInvAddrOffhand, v.offhand);
        for (int i = 0; i < 4; i++)
            slots += Cell("c" + std::to_string(i), px, py, kInvAddrCraft + i, v.craft[static_cast<size_t>(i)]);
        slots += Cell("co", px, py, kInvAddrOutput, v.output);
        for (int i = 0; i < 36; i++)
            slots += Cell("s" + std::to_string(i), px, py, i, v.slots[static_cast<size_t>(i)]);

        std::string cursor;
        if (!v.cursor.Empty()) {
            cursor = "  <div id=\"cursor\" style=\"left:" + std::to_string(static_cast<int>(v.mouseX) - 8) + "px;top:" +
                     std::to_string(static_cast<int>(v.mouseY) - 8) + "px;\"><mcitem key=\"" + EscapeRml(v.cursor.Key()) +
                     "\" style=\"left:0px;top:0px;width:16px;height:16px;\"" + (v.cursor.enchanted ? " glint=\"1\"" : "") + "/>";
            if (v.cursor.count > 1)
                cursor += "<div class=\"cnt\" style=\"left:-3px;top:9px;width:19px;height:9px;\">" + std::to_string(v.cursor.count) + "</div>";
            cursor += "</div>\n";
        }

        std::string tip;
        if (v.cursor.Empty() && v.hovered >= 0) {
            const HudItem* it = nullptr;
            if (v.hovered < 36) it = &v.slots[static_cast<size_t>(v.hovered)];
            else if (v.hovered < kInvAddrOffhand) it = &v.armor[static_cast<size_t>(v.hovered - kInvAddrArmor)];
            else if (v.hovered == kInvAddrOffhand) it = &v.offhand;
            else if (v.hovered < kInvAddrOutput) it = &v.craft[static_cast<size_t>(v.hovered - kInvAddrCraft)];
            else it = &v.output;
            if (!it->Empty()) {
                const int x = static_cast<int>(v.mouseX) + 12 + 3, y = static_cast<int>(v.mouseY) - 12 - 3;
                tip = "  <div id=\"tip\" style=\"left:" + std::to_string(x) + "px;top:" + std::to_string(y) + "px;\"><div class=\"edge\"></div><div class=\"txt\">" +
                      EscapeRml(PrettyItemName(it->name)) + "</div></div>\n";
            }
        }

        std::string d = templateText;
        ReplaceAll(d, "w", std::to_string(guiW));
        ReplaceAll(d, "h", std::to_string(guiH));
        ReplaceAll(d, "x", std::to_string(px));
        ReplaceAll(d, "y", std::to_string(py));
        ReplaceAll(d, "lx", std::to_string(px + 97));
        ReplaceAll(d, "ly", std::to_string(py + 8));
        ReplaceAll(d, "title", EscapeRml(v.title.empty() ? i18n::Tr("container.crafting") : v.title));
        ReplaceAll(d, "chrome", Chrome());
        ReplaceAll(d, "slots", slots);
        ReplaceAll(d, "cursor", cursor);
        ReplaceAll(d, "tooltip", tip);
        return d;
    }
}
