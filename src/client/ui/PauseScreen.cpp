#include "client/ui/PauseScreen.hpp"

#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiTemplate.hpp"

namespace perlin::ui {
    std::string DefaultPauseScreenTemplate() {
        return R"PERLINRML(<rml>
<head>
<title>pause</title>
<style>
/* Edit this file while the game runs: it is reloaded when it changes. {{name}} placeholders are filled in by Perlin. */
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }
div { display: block; position: absolute; }
#dim { left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; background-color: rgba(16,16,16,160); }
#heading { left: 0px; top: 15px; width: {{w}}px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; z-index: 3; }
.btn { width: 204px; height: 20px; background-color: #000000; z-index: 5; }
.btn div { position: absolute; }
.btn .rim { left: 1px; top: 1px; width: 201px; height: 16px; background-color: #aaaaaa; }
.btn .face { left: 2px; top: 2px; width: 200px; height: 15px; background-color: #6f6f6f; }
.btn .shade { left: 1px; top: 17px; width: 202px; height: 2px; background-color: #565656; }
.btn .edge { left: 202px; top: 1px; width: 1px; height: 16px; background-color: #565656; }
.btn .lbl { font-family: minecraft-shadow; left: 0px; top: 6px; width: 204px; height: 9px; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; }
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
  <div id="resume" class="btn" style="left: {{btn_x}}px; top: {{y1}}px;">
    <div class="rim"></div><div class="face"></div><div class="shade"></div><div class="edge"></div>
    <div class="lbl">{{back}}</div>
  </div>
  <div id="options" class="btn{{options_off}}" style="left: {{btn_x}}px; top: {{y2}}px;">
    <div class="rim"></div><div class="face"></div><div class="shade"></div><div class="edge"></div>
    <div class="lbl">{{options}}</div>
  </div>
  <div id="quit" class="btn" style="left: {{btn_x}}px; top: {{y3}}px;">
    <div class="rim"></div><div class="face"></div><div class="shade"></div><div class="edge"></div>
    <div class="lbl">{{quit}}</div>
  </div>
</body>
</rml>
)PERLINRML";
    }

    std::string BuildPauseScreenRml(const std::string& templateText, const PauseScreenText& t, int guiW, int guiH) {
        std::string d = templateText;
        ReplaceAll(d, "w", std::to_string(guiW));
        ReplaceAll(d, "h", std::to_string(guiH));
        ReplaceAll(d, "btn_x", std::to_string(guiW / 2 - 102));
        ReplaceAll(d, "y1", std::to_string(guiH / 4 + 24));
        ReplaceAll(d, "y2", std::to_string(guiH / 4 + 48));
        ReplaceAll(d, "y3", std::to_string(guiH / 4 + 72));
        ReplaceAll(d, "options_off", t.optionsEnabled ? "" : " off");
        ReplaceAll(d, "title", EscapeRml(t.title));
        ReplaceAll(d, "back", EscapeRml(t.back));
        ReplaceAll(d, "options", EscapeRml(t.options));
        ReplaceAll(d, "quit", EscapeRml(t.quit));
        return d;
    }
}
