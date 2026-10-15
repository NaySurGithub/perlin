#include "client/ui/InventoryScreen.hpp"

#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiSprite.hpp"
#include "client/ui/UiTemplate.hpp"
#include "client/i18n/Lang.hpp"

#include <algorithm>
#include <cctype>
#include <vector>
#include <cstdlib>
#include <cstdio>

namespace perlin::ui {
    std::string DefaultInventoryScreenTemplate() {
        return R"PERLINRML(<!--perlin-ui:6-->
<rml>
<head>
<title>inventory</title>
<style>
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }
div { display: block; position: absolute; }
mcitem { display: block; position: absolute; }
#dim { left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; background-color: rgba(16,16,16,200); }
#panel { left: {{x}}px; top: {{y}}px; width: 176px; height: 166px; }
.r { position: absolute; }
.label { font-family: minecraft; font-size: 8px; line-height: 9px; color: #404040; white-space: nowrap; }
.slot { width: 18px; height: 18px; z-index: 2; }
.slot .pb { display: none; left: 0px; top: 0px; width: 18px; height: 18px; z-index: 1; pointer-events: none; }
.slot:hover .pb { display: block; }
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
  <div id="top" style="left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; z-index: 50; pointer-events: none;"><mclayer layer="top" style="display: block; position: absolute; width: 0px; height: 0px; z-index: -999;"></mclayer>{{cursor}}{{tooltip}}<mclayer layer="gui" style="display: block; position: absolute; width: 0px; height: 0px; z-index: 999;"></mclayer></div>
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
            return SpriteRml("ui_inventory.png", 0, 0, 176, 166, 0, 0);
        }

        std::string Cell(const std::string& id, int px, int py, int addr, const HudItem& item) {
            int x, y;
            InventorySlotPos(addr, x, y);
            std::string s = "  <div id=\"" + id + "\" class=\"slot\" style=\"left:" + std::to_string(px + x) + "px;top:" + std::to_string(py + y) +
                            "px;\">";
            if (item.Empty() && ((addr >= kInvAddrArmor && addr < kInvAddrOffhand) || addr == kInvAddrOffhand)) {
                const int icon = addr == kInvAddrOffhand ? 4 : addr - kInvAddrArmor;
                s += SpriteRml("ui_slot_icons.png", icon * 16, 0, 16, 16, 1, 1);
            }
            if (!item.Empty()) {
                s += "<mcitem key=\"" + EscapeRml(item.Key()) + "\" style=\"left:1px;top:1px;width:16px;height:16px;\"";
                if (item.enchanted)
                    s += " glint=\"1\"";
                s += "/>";
                if (item.count > 1)
                    s += "<div class=\"cnt\" style=\"left:-1px;top:10px;width:19px;height:9px;\">" + std::to_string(item.count) + "</div>";
            }
            s += "<mcimg class=\"pb\" src=\"textures/ui/purpleBorder\" nine=\"4 4 4 4\"/></div>\n";
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
