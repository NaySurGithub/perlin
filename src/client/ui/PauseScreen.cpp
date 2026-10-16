#include "client/ui/PauseScreen.hpp"
#include <cmath>
#include <algorithm>

#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiTemplate.hpp"

namespace perlin::ui {
    std::string DefaultPauseScreenTemplate() {
        return R"PERLINRML(<!--perlin-ui:pause-6-->
<rml>
<head>
<title>pause</title>
<style>
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }
div { display: block; position: absolute; }
#dim { left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; background-color: rgba(0,0,0,64); }
#heading { left: 0px; top: 40px; width: {{w}}px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; z-index: 3; }
.btn { height: 20px; background-color: #000000; z-index: 5; }
.btn div { position: absolute; }
.btn .rim { left: 1px; top: 1px; height: 16px; background-color: #aaaaaa; }
.btn .face { left: 2px; top: 2px; height: 15px; background-color: #6f6f6f; }
.btn .shade { left: 1px; top: 17px; height: 2px; background-color: #565656; }
.btn .edge { top: 1px; width: 1px; height: 16px; background-color: #565656; }
.btn .lbl { font-family: minecraft-shadow; left: 0px; top: 6px; height: 9px; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; }
.btn:hover { background-color: #ffffff; }
.btn:hover .rim { background-color: #afafaf; }
.btn:hover .face { background-color: #757575; }
.btn:hover .shade, .btn:hover .edge { background-color: #5c5c5c; }
.btn.off, .btn.off:hover { background-color: #000000; }
.btn.off .rim, .btn.off .face, .btn.off .shade, .btn.off .edge { background-color: #2c2c2c; }
.btn.off .lbl { color: #a0a0a0; }
</style>
</head>
<body>
  <div id="dim"></div>
  <div id="heading">{{title}}</div>
  <div id="resume" class="btn" style="left: {{x1}}px; top: {{y1}}px; width: 204px;">
    <div class="rim" style="width: 201px;"></div><div class="face" style="width: 200px;"></div><div class="shade" style="width: 202px;"></div><div class="edge" style="left: 202px;"></div>
    <div class="lbl" style="width: 204px;">{{back}}</div>
  </div>
  <div id="feedback" class="btn" style="left: {{x1}}px; top: {{y2}}px; width: 100px;">
    <div class="rim" style="width: 97px;"></div><div class="face" style="width: 96px;"></div><div class="shade" style="width: 98px;"></div><div class="edge" style="left: 98px;"></div>
    <div class="lbl" style="width: 100px;">{{feedback}}</div>
  </div>
  <div id="players" class="btn" style="left: {{x2}}px; top: {{y2}}px; width: 100px;">
    <div class="rim" style="width: 97px;"></div><div class="face" style="width: 96px;"></div><div class="shade" style="width: 98px;"></div><div class="edge" style="left: 98px;"></div>
    <div class="lbl" style="width: 100px;">{{players}}</div>
  </div>
  <div id="options" class="btn" style="left: {{x1}}px; top: {{y3}}px; width: 100px;">
    <div class="rim" style="width: 97px;"></div><div class="face" style="width: 96px;"></div><div class="shade" style="width: 98px;"></div><div class="edge" style="left: 98px;"></div>
    <div class="lbl" style="width: 100px;">{{options}}</div>
  </div>
  <div id="world" class="btn" style="left: {{x2}}px; top: {{y3}}px; width: 100px;">
    <div class="rim" style="width: 97px;"></div><div class="face" style="width: 96px;"></div><div class="shade" style="width: 98px;"></div><div class="edge" style="left: 98px;"></div>
    <div class="lbl" style="width: 100px;">{{world}}</div>
  </div>
  <div id="quit" class="btn" style="left: {{x1}}px; top: {{y4}}px; width: 204px;">
    <div class="rim" style="width: 201px;"></div><div class="face" style="width: 200px;"></div><div class="shade" style="width: 202px;"></div><div class="edge" style="left: 202px;"></div>
    <div class="lbl" style="width: 204px;">{{quit}}</div>
  </div>
</body>
</rml>
)PERLINRML";
    }

    namespace {
        std::string Button(const std::string& id, float x, float y, float w, const std::string& label, bool off) {
            const auto px = [](float v) { return std::to_string(static_cast<int>(std::lround(v))); };
            std::string d = "<div id=\"" + id + "\" class=\"btn" + (off ? " off" : "") + "\" style=\"left: " + px(x) + "px; top: " + px(y) + "px; width: " + px(w) + "px;\">";
            d += "<div class=\"rim\" style=\"width: " + px(w - 3) + "px;\"></div><div class=\"face\" style=\"width: " + px(w - 4) + "px;\"></div>";
            d += "<div class=\"shade\" style=\"width: " + px(w - 2) + "px;\"></div><div class=\"edge\" style=\"left: " + px(w - 2) + "px;\"></div>";
            d += "<div class=\"lbl\" style=\"width: " + px(w) + "px;\">" + EscapeRml(label) + "</div></div>\n";
            return d;
        }

        std::string Slider(const std::string& id, float x, float y, float w, const std::string& label, float fraction) {
            const auto px = [](float v) { return std::to_string(static_cast<int>(std::lround(v))); };
            const float handle = x + 1.0f + std::clamp(fraction, 0.0f, 1.0f) * (w - 10.0f);
            std::string d = "<div id=\"" + id + "\" class=\"btn track\" style=\"left: " + px(x) + "px; top: " + px(y) + "px; width: " + px(w) + "px;\">";
            d += "<div class=\"rim\" style=\"width: " + px(w - 3) + "px;\"></div><div class=\"face\" style=\"width: " + px(w - 4) + "px;\"></div>";
            d += "<div class=\"lbl\" style=\"width: " + px(w) + "px; z-index: 2;\">" + EscapeRml(label) + "</div>";
            d += "<div class=\"knob\" style=\"left: " + px(handle - x) + "px; top: 0px;\"><div class=\"krim\"></div><div class=\"kface\"></div><div class=\"kshade\"></div></div></div>\n";
            return d;
        }
    }

    OptionsLayout ComputeOptionsLayout(int count, int guiW, int guiH) {
        OptionsLayout l;
        l.count = std::min(count, 16);
        const int perColumn = (l.count + 1) / 2;
        const float start = std::max(36.0f, static_cast<float>(guiH) / 6.0f - 12.0f);
        for (int i = 0; i < l.count; i++) {
            const int col = i % 2, row = i / 2;
            l.x[i] = static_cast<float>(guiW) / 2.0f - 155.0f + static_cast<float>(col) * 160.0f;
            l.y[i] = start + static_cast<float>(row) * 24.0f;
        }
        l.doneW = 200.0f;
        l.doneX = static_cast<float>(guiW) / 2.0f - 100.0f;
        l.doneY = std::min(start + static_cast<float>(perColumn) * 24.0f + 12.0f, static_cast<float>(guiH) - 27.0f);
        return l;
    }

    std::string BuildOptionsScreenRml(const std::vector<OptionsRow>& rows, const std::string& title, const std::string& done,
                                      int guiW, int guiH) {
        const OptionsLayout l = ComputeOptionsLayout(static_cast<int>(rows.size()), guiW, guiH);
        std::string d = R"PERLINRML(<rml>
<head>
<title>options</title>
<style>
body { position: absolute; left: 0px; top: 0px; width: )PERLINRML" + std::to_string(guiW) + "px; height: " + std::to_string(guiH) + R"PERLINRML(px; margin: 0; padding: 0; background: transparent; }
div { display: block; position: absolute; }
#dim { left: 0px; top: 0px; width: )PERLINRML" + std::to_string(guiW) + "px; height: " + std::to_string(guiH) + R"PERLINRML(px; background-color: rgba(0,0,0,128); }
#heading { left: 0px; top: 15px; width: )PERLINRML" + std::to_string(guiW) + R"PERLINRML(px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; z-index: 3; }
.btn { height: 20px; background-color: #000000; z-index: 5; }
.btn div { position: absolute; }
.btn .rim { left: 1px; top: 1px; height: 16px; background-color: #aaaaaa; }
.btn .face { left: 2px; top: 2px; height: 15px; background-color: #6f6f6f; }
.btn .shade { left: 1px; top: 17px; height: 2px; background-color: #565656; }
.btn .edge { top: 1px; width: 1px; height: 16px; background-color: #565656; }
.btn .lbl { font-family: minecraft-shadow; left: 0px; top: 6px; height: 9px; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; }
.btn:hover { background-color: #ffffff; }
.btn:hover .rim { background-color: #afafaf; }
.btn:hover .face { background-color: #757575; }
.btn:hover .shade, .btn:hover .edge { background-color: #5c5c5c; }
.btn.track .rim { background-color: #2c2c2c; }
.btn.track .face { background-color: #1f1f1f; }
.btn.track:hover { background-color: #ffffff; }
.btn.track:hover .rim { background-color: #2c2c2c; }
.btn.track:hover .face { background-color: #1f1f1f; }
.knob { width: 8px; height: 20px; z-index: 1; }
.knob .krim { left: 0px; top: 0px; width: 8px; height: 20px; background-color: #000000; }
.knob .kface { left: 1px; top: 1px; width: 6px; height: 16px; background-color: #aaaaaa; }
.knob .kshade { left: 1px; top: 17px; width: 6px; height: 2px; background-color: #565656; }
.btn.track:hover .kface { background-color: #bdbdbd; }
</style>
</head>
<body>
<div id="dim"></div>
<div id="heading">)PERLINRML" + EscapeRml(title) + "</div>\n";
        for (int i = 0; i < l.count; i++) {
            const OptionsRow& row = rows[static_cast<size_t>(i)];
            const std::string label = row.value.empty() ? row.label : row.label + ": " + row.value;
            if (row.fraction >= 0.0f)
                d += Slider("row" + std::to_string(i), l.x[i], l.y[i], l.w, label, row.fraction);
            else
                d += Button("row" + std::to_string(i), l.x[i], l.y[i], l.w, label, false);
        }
        d += Button(kOptionsDoneId, l.doneX, l.doneY, l.doneW, done, false);
        d += "</body>\n</rml>\n";
        return d;
    }

    KeybindLayout ComputeKeybindLayout(int rows, int guiW, int guiH) {
        KeybindLayout l;
        l.listBottom = static_cast<float>(guiH) - 32.0f;
        const float cx = static_cast<float>(guiW) / 2.0f;
        l.labelX = cx - 160.0f;
        l.keyX = cx + 20.0f;
        l.resetX = cx + 100.0f;
        l.doneW = 200.0f;
        l.doneX = cx - 100.0f;
        l.doneY = static_cast<float>(guiH) - 26.0f;
        l.contentH = static_cast<float>(rows) * l.pitch + 12.0f;
        l.maxScroll = std::max(0.0f, l.contentH - (l.listBottom - l.listTop));
        return l;
    }

    std::string BuildKeybindsScreenRml(const std::vector<KeybindRow>& rows, const std::string& title, const std::string& done,
                                       const std::string& reset, float scroll, int guiW, int guiH) {
        const KeybindLayout l = ComputeKeybindLayout(static_cast<int>(rows.size()), guiW, guiH);
        const auto px = [](float v) { return std::to_string(static_cast<int>(std::lround(v))); };
        const float listH = l.listBottom - l.listTop;
        std::string d = R"PERLINRML(<rml>
<head>
<title>keybinds</title>
<style>
body { position: absolute; left: 0px; top: 0px; width: )PERLINRML" + px(guiW) + "px; height: " + px(guiH) + R"PERLINRML(px; margin: 0; padding: 0; background: transparent; }
div { display: block; position: absolute; }
#dim { left: 0px; top: 0px; width: )PERLINRML" + px(guiW) + "px; height: " + px(guiH) + R"PERLINRML(px; background-color: rgba(0,0,0,128); }
#heading { left: 0px; top: 12px; width: )PERLINRML" + px(guiW) + R"PERLINRML(px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; z-index: 3; }
#list { overflow: hidden; background-color: rgba(0,0,0,112); }
.btn { height: 20px; background-color: #000000; z-index: 5; }
.btn div { position: absolute; }
.btn .rim { left: 1px; top: 1px; height: 16px; background-color: #aaaaaa; }
.btn .face { left: 2px; top: 2px; height: 15px; background-color: #6f6f6f; }
.btn .shade { left: 1px; top: 17px; height: 2px; background-color: #565656; }
.btn .edge { top: 1px; width: 1px; height: 16px; background-color: #565656; }
.btn .lbl { font-family: minecraft-shadow; left: 0px; top: 6px; height: 9px; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; }
.btn:hover { background-color: #ffffff; }
.btn:hover .rim { background-color: #afafaf; }
.btn:hover .face { background-color: #757575; }
.btn:hover .shade, .btn:hover .edge { background-color: #5c5c5c; }
.btn.off, .btn.off:hover { background-color: #000000; }
.btn.off .rim, .btn.off .face, .btn.off .shade, .btn.off .edge { background-color: #2c2c2c; }
.btn.off .lbl { color: #a0a0a0; }
.row { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; height: 9px; color: #ffffff; white-space: nowrap; }
.head { text-align: center; }
.sep { height: 1px; background-color: rgba(255,255,255,51); }
.sepb { height: 1px; background-color: rgba(0,0,0,191); }
.track { background-color: rgba(0,0,0,255); width: 6px; }
.thumb { background-color: #808080; width: 6px; }
</style>
</head>
<body>
<div id="dim"></div>
<div id="heading">)PERLINRML" + EscapeRml(title) + "</div>\n";
        d += "<div id=\"list\" style=\"left: 0px; top: " + px(l.listTop) + "px; width: " + px(guiW) + "px; height: " + px(listH) + "px;\">\n";
        const auto key = [&](int i, int which, float x, float w, const std::string& label, bool off, const char* color) {
            const std::string id = "kb" + std::to_string(which) + "_" + std::to_string(i);
            const float y = l.RowY(i, scroll) - l.listTop;
            std::string b = "<div id=\"" + id + "\" class=\"btn" + (off ? " off" : "") + "\" style=\"left: " + px(x) + "px; top: " + px(y) + "px; width: " + px(w) + "px;\">";
            b += "<div class=\"rim\" style=\"width: " + px(w - 3) + "px;\"></div><div class=\"face\" style=\"width: " + px(w - 4) + "px;\"></div>";
            b += "<div class=\"shade\" style=\"width: " + px(w - 2) + "px;\"></div><div class=\"edge\" style=\"left: " + px(w - 2) + "px;\"></div>";
            b += "<div class=\"lbl\" style=\"width: " + px(w) + "px;" + (color ? std::string(" color: ") + color + ";" : std::string()) + "\">" + EscapeRml(label) + "</div></div>\n";
            return b;
        };
        for (int i = 0; i < static_cast<int>(rows.size()); i++) {
            const KeybindRow& r = rows[static_cast<size_t>(i)];
            const float y = l.RowY(i, scroll) - l.listTop;
            if (y < 0.0f || y + 20.0f > listH)
                continue;
            if (r.header) {
                d += "<div class=\"row head\" style=\"left: 0px; top: " + px(y + 6.0f) + "px; width: " + px(guiW) + "px;\">" + EscapeRml(r.label) + "</div>\n";
                continue;
            }
            d += "<div class=\"row\" style=\"left: " + px(l.labelX) + "px; top: " + px(y + 6.0f) + "px; width: 175px;\">" + EscapeRml(r.label) + "</div>\n";
            d += key(i, 0, l.keyX, l.keyW, r.value, false, r.color == 1 ? "#ff5555" : r.color == 2 ? "#ffff55" : nullptr);
            d += key(i, 1, l.resetX, l.resetW, reset, !r.canReset, nullptr);
        }
        d += "</div>\n";
        d += "<div class=\"sepb\" style=\"left: 0px; top: " + px(l.listTop - 1.0f) + "px; width: " + px(guiW) + "px;\"></div>\n";
        d += "<div class=\"sep\" style=\"left: 0px; top: " + px(l.listTop - 2.0f) + "px; width: " + px(guiW) + "px;\"></div>\n";
        d += "<div class=\"sepb\" style=\"left: 0px; top: " + px(l.listBottom) + "px; width: " + px(guiW) + "px;\"></div>\n";
        d += "<div class=\"sep\" style=\"left: 0px; top: " + px(l.listBottom + 1.0f) + "px; width: " + px(guiW) + "px;\"></div>\n";
        if (l.maxScroll > 0.0f) {
            const float th = std::max(16.0f, listH * listH / l.contentH);
            const float ty = l.listTop + (listH - th) * (scroll / l.maxScroll);
            d += "<div class=\"track\" style=\"left: " + px(guiW / 2 + 160) + "px; top: " + px(l.listTop) + "px; height: " + px(listH) + "px;\"></div>\n";
            d += "<div class=\"thumb\" style=\"left: " + px(guiW / 2 + 160) + "px; top: " + px(ty) + "px; height: " + px(th) + "px;\"></div>\n";
        }
        d += Button(kKeybindDoneId, l.doneX, l.doneY, l.doneW, done, false);
        d += "</body>\n</rml>\n";
        return d;
    }

    std::string BuildPauseScreenRml(const std::string& templateText, const PauseScreenText& t, int guiW, int guiH) {
        std::string d = templateText;
        const int top = (guiH - 130) / 4;
        const int x1 = guiW / 2 - 102;
        ReplaceAll(d, "w", std::to_string(guiW));
        ReplaceAll(d, "h", std::to_string(guiH));
        ReplaceAll(d, "x1", std::to_string(x1));
        ReplaceAll(d, "x2", std::to_string(x1 + 104));
        ReplaceAll(d, "y1", std::to_string(top + 54));
        ReplaceAll(d, "y2", std::to_string(top + 78));
        ReplaceAll(d, "y3", std::to_string(top + 102));
        ReplaceAll(d, "y4", std::to_string(top + 126));
        ReplaceAll(d, "title", EscapeRml(t.title));
        ReplaceAll(d, "back", EscapeRml(t.back));
        ReplaceAll(d, "feedback", EscapeRml(t.feedback));
        ReplaceAll(d, "players", EscapeRml(t.players));
        ReplaceAll(d, "options", EscapeRml(t.options));
        ReplaceAll(d, "world", EscapeRml(t.world));
        ReplaceAll(d, "quit", EscapeRml(t.quit));
        if (t.showPanel) {
            const int pw = 150;
            const int rows = std::max<int>(1, static_cast<int>(t.panel.size()));
            const int shown = std::min(rows, (guiH - 40) / 14);
            const int ph = 22 + shown * 14 + 4;
            const int px = guiW - pw - 6;
            const int py = std::max(4, (guiH - ph) / 2);
            std::string b = "<div id=\"pp\" style=\"left: " + std::to_string(px) + "px; top: " + std::to_string(py) + "px; width: " + std::to_string(pw) + "px; height: " + std::to_string(ph) + "px; background-color: #000000; z-index: 6;\">";
            b += "<div style=\"left: 1px; top: 1px; width: " + std::to_string(pw - 2) + "px; height: " + std::to_string(ph - 2) + "px; background-color: #c6c6c6;\"></div>";
            b += "<div style=\"left: 1px; top: 1px; width: " + std::to_string(pw - 3) + "px; height: " + std::to_string(ph - 3) + "px; background-color: #ffffff;\"></div>";
            b += "<div style=\"left: 2px; top: 2px; width: " + std::to_string(pw - 4) + "px; height: " + std::to_string(ph - 4) + "px; background-color: #373737;\"></div>";
            b += "<div style=\"left: 0px; top: 8px; width: " + std::to_string(pw) + "px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap;\">" + EscapeRml(t.panelTitle) + "</div>";
            for (int i = 0; i < shown && i < static_cast<int>(t.panel.size()); i++) {
                const PauseScreenText::PanelRow& r = t.panel[static_cast<size_t>(i)];
                const char* color = r.perm >= 2 ? "#ffaa00" : (r.perm == 0 ? "#aaaaaa" : "#55ff55");
                const std::string& role = r.perm >= 2 ? t.roleOperator : (r.perm == 0 ? t.roleVisitor : t.roleMember);
                const int ry = 22 + i * 14;
                b += "<div style=\"left: 8px; top: " + std::to_string(ry) + "px; width: 80px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; color: #ffffff; white-space: nowrap; overflow: hidden;\">" + EscapeRml(r.name) + "</div>";
                b += "<div style=\"left: 88px; top: " + std::to_string(ry) + "px; width: " + std::to_string(pw - 96) + "px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: right; color: " + color + "; white-space: nowrap;\">" + EscapeRml(role) + "</div>";
            }
            b += "</div>\n";
            const size_t at = d.rfind("</body>");
            if (at != std::string::npos)
                d.insert(at, b);
        }
        return d;
    }
}
