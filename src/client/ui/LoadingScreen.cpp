#include "client/ui/LoadingScreen.hpp"

#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiTemplate.hpp"

#include <algorithm>

namespace perlin::ui {
    std::string DefaultLoadingScreenTemplate() {
        return R"PERLINRML(<rml>
<head>
<title>loading</title>
<style>
/* Edit this file while the game runs: it is reloaded when it changes. {{name}} placeholders are filled in by Perlin. */
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }
div { display: block; position: absolute; }
mcimg { display: block; position: absolute; }
#heading { left: 0px; top: {{title_y}}px; width: {{w}}px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; z-index: 3; }
#message { left: 0px; top: {{msg_y}}px; width: {{w}}px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; z-index: 3; }
#detail { left: {{detail_x}}px; top: {{detail_y}}px; width: {{detail_w}}px; height: 27px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #8a8a8a; z-index: 3; }
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
</style>
</head>
<body>
  <mcimg src="textures/blocks/dirt" tiled="1" tilescale="{{tile_scale}}" color="0.25 0.25 0.25" style="left: 0px; top: 0px; width: {{w}}px; height: {{h}}px;"></mcimg>
  <div id="heading">{{title}}</div>
  <div id="message">{{message}}</div>
  <div id="detail">{{detail}}</div>
  <div id="cancel" class="btn" style="left: {{btn_x}}px; top: {{btn_y}}px;">
    <div class="rim"></div><div class="face"></div><div class="shade"></div><div class="edge"></div>
    <div class="lbl">{{cancel}}</div>
  </div>
</body>
</rml>
)PERLINRML";
    }

    std::string BuildLoadingScreenRml(const std::string& templateText, const LoadingScreenText& t, int guiW, int guiH, double) {
        const int titleY = guiH / 2 - 30;
        const int msgY = titleY + 14;
        const int detailY = msgY + 14;
        const int btnY = std::min(guiH - 24, detailY + 36);
        std::string d = templateText;
        ReplaceAll(d, "w", std::to_string(guiW));
        ReplaceAll(d, "h", std::to_string(guiH));
        ReplaceAll(d, "tile_scale", "2");
        ReplaceAll(d, "title_y", std::to_string(titleY));
        ReplaceAll(d, "msg_y", std::to_string(msgY));
        ReplaceAll(d, "detail_x", std::to_string(std::max(10, (guiW - 300) / 2)));
        ReplaceAll(d, "detail_y", std::to_string(detailY));
        ReplaceAll(d, "detail_w", std::to_string(std::min(300, guiW - 20)));
        ReplaceAll(d, "btn_x", std::to_string(guiW / 2 - 102));
        ReplaceAll(d, "btn_y", std::to_string(btnY));
        ReplaceAll(d, "cancel", EscapeRml(t.cancel.empty() ? std::string("Cancel") : t.cancel));
        ReplaceAll(d, "title", EscapeRml(t.title));
        ReplaceAll(d, "message", EscapeRml(t.message));
        ReplaceAll(d, "detail", EscapeRml(t.detail));
        return d;
    }
}
