#include "client/ui/PauseScreen.hpp"

#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiTemplate.hpp"

namespace perlin::ui {
    std::string DefaultPauseScreenTemplate() {
        return R"PERLINRML(<!--perlin-ui:pause-2-->
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
  <div id="advancements" class="btn off" style="left: {{x1}}px; top: {{y2}}px; width: 98px;">
    <div class="rim" style="width: 95px;"></div><div class="face" style="width: 94px;"></div><div class="shade" style="width: 96px;"></div><div class="edge" style="left: 96px;"></div>
    <div class="lbl" style="width: 98px;">{{advancements}}</div>
  </div>
  <div id="stats" class="btn off" style="left: {{x2}}px; top: {{y2}}px; width: 98px;">
    <div class="rim" style="width: 95px;"></div><div class="face" style="width: 94px;"></div><div class="shade" style="width: 96px;"></div><div class="edge" style="left: 96px;"></div>
    <div class="lbl" style="width: 98px;">{{stats}}</div>
  </div>
  <div id="feedback" class="btn off" style="left: {{x1}}px; top: {{y3}}px; width: 98px;">
    <div class="rim" style="width: 95px;"></div><div class="face" style="width: 94px;"></div><div class="shade" style="width: 96px;"></div><div class="edge" style="left: 96px;"></div>
    <div class="lbl" style="width: 98px;">{{feedback}}</div>
  </div>
  <div id="bugs" class="btn off" style="left: {{x2}}px; top: {{y3}}px; width: 98px;">
    <div class="rim" style="width: 95px;"></div><div class="face" style="width: 94px;"></div><div class="shade" style="width: 96px;"></div><div class="edge" style="left: 96px;"></div>
    <div class="lbl" style="width: 98px;">{{bugs}}</div>
  </div>
  <div id="options" class="btn{{options_off}}" style="left: {{x1}}px; top: {{y4}}px; width: 98px;">
    <div class="rim" style="width: 95px;"></div><div class="face" style="width: 94px;"></div><div class="shade" style="width: 96px;"></div><div class="edge" style="left: 96px;"></div>
    <div class="lbl" style="width: 98px;">{{options}}</div>
  </div>
  <div id="lan" class="btn off" style="left: {{x2}}px; top: {{y4}}px; width: 98px;">
    <div class="rim" style="width: 95px;"></div><div class="face" style="width: 94px;"></div><div class="shade" style="width: 96px;"></div><div class="edge" style="left: 96px;"></div>
    <div class="lbl" style="width: 98px;">{{lan}}</div>
  </div>
  <div id="quit" class="btn" style="left: {{x1}}px; top: {{y5}}px; width: 204px;">
    <div class="rim" style="width: 201px;"></div><div class="face" style="width: 200px;"></div><div class="shade" style="width: 202px;"></div><div class="edge" style="left: 202px;"></div>
    <div class="lbl" style="width: 204px;">{{quit}}</div>
  </div>
</body>
</rml>
)PERLINRML";
    }

    std::string BuildPauseScreenRml(const std::string& templateText, const PauseScreenText& t, int guiW, int guiH) {
        std::string d = templateText;
        const int top = (guiH - 170) / 4;
        const int x1 = guiW / 2 - 102;
        const int x2 = guiW / 2 + 4;
        ReplaceAll(d, "w", std::to_string(guiW));
        ReplaceAll(d, "h", std::to_string(guiH));
        ReplaceAll(d, "x1", std::to_string(x1));
        ReplaceAll(d, "x2", std::to_string(x2));
        ReplaceAll(d, "y1", std::to_string(top + 54));
        ReplaceAll(d, "y2", std::to_string(top + 78));
        ReplaceAll(d, "y3", std::to_string(top + 102));
        ReplaceAll(d, "y4", std::to_string(top + 126));
        ReplaceAll(d, "y5", std::to_string(top + 150));
        ReplaceAll(d, "options_off", t.optionsEnabled ? "" : " off");
        ReplaceAll(d, "title", EscapeRml(t.title));
        ReplaceAll(d, "back", EscapeRml(t.back));
        ReplaceAll(d, "advancements", EscapeRml(t.advancements));
        ReplaceAll(d, "stats", EscapeRml(t.stats));
        ReplaceAll(d, "feedback", EscapeRml(t.feedback));
        ReplaceAll(d, "bugs", EscapeRml(t.bugs));
        ReplaceAll(d, "options", EscapeRml(t.options));
        ReplaceAll(d, "lan", EscapeRml(t.lan));
        ReplaceAll(d, "quit", EscapeRml(t.quit));
        return d;
    }
}
