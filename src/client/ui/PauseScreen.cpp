#include "client/ui/PauseScreen.hpp"
#include <cmath>
#include <algorithm>

#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiTemplate.hpp"

namespace perlin::ui {
    std::string DefaultPauseScreenTemplate() {
        return R"PERLINRML(<!--perlin-ui:pause-5-->
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
  <div id="options" class="btn" style="left: {{x1}}px; top: {{y3}}px; width: 204px;">
    <div class="rim" style="width: 201px;"></div><div class="face" style="width: 200px;"></div><div class="shade" style="width: 202px;"></div><div class="edge" style="left: 202px;"></div>
    <div class="lbl" style="width: 204px;">{{options}}</div>
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
        ReplaceAll(d, "quit", EscapeRml(t.quit));
        return d;
    }
}
