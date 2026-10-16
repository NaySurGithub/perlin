#include "client/ui/LoadingScreen.hpp"

#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiTemplate.hpp"

#include <algorithm>
#include <cmath>

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
#barback { left: {{bar_x}}px; top: {{bar_y}}px; width: {{bar_w}}px; height: 4px; background-color: #000000; z-index: 3; }
#barfill { left: {{bar_x}}px; top: {{bar_y}}px; width: {{fill_w}}px; height: 4px; background-color: #80ff20; z-index: 4; }
#barglow { left: {{glow_x}}px; top: {{bar_y}}px; width: {{glow_w}}px; height: 4px; background-color: #80ff20; z-index: 4; }
</style>
</head>
<body>
  <mcimg src="textures/blocks/dirt" tiled="1" tilescale="{{tile_scale}}" color="0.25 0.25 0.25" style="left: 0px; top: 0px; width: {{w}}px; height: {{h}}px;"></mcimg>
  <div id="heading">{{title}}</div>
  <div id="message">{{message}}</div>
  <div id="barback"></div>
  <div id="barfill"></div>
  <div id="barglow"></div>
  <div id="detail">{{detail}}</div>
</body>
</rml>
)PERLINRML";
    }

    std::string BuildLoadingScreenRml(const std::string& templateText, const LoadingScreenText& t, int guiW, int guiH, double time) {
        const int barW = std::min(guiW - 40, 200);
        const int barX = (guiW - barW) / 2;
        const int titleY = guiH / 2 - 30;
        const int msgY = titleY + 14;
        const int barY = msgY + 16;
        int fillW = 0, glowX = barX, glowW = 0;
        if (t.progress >= 0.0f) {
            fillW = static_cast<int>(std::clamp(t.progress, 0.0f, 1.0f) * static_cast<float>(barW));
        } else {
            glowW = std::max(8, barW / 4);
            const double phase = std::fmod(time * 0.9, 1.0);
            glowX = barX + static_cast<int>(phase * static_cast<double>(barW - glowW));
        }
        std::string d = templateText;
        ReplaceAll(d, "w", std::to_string(guiW));
        ReplaceAll(d, "h", std::to_string(guiH));
        ReplaceAll(d, "tile_scale", "2");
        ReplaceAll(d, "title_y", std::to_string(titleY));
        ReplaceAll(d, "msg_y", std::to_string(msgY));
        ReplaceAll(d, "bar_x", std::to_string(barX));
        ReplaceAll(d, "bar_y", std::to_string(barY));
        ReplaceAll(d, "bar_w", std::to_string(barW));
        ReplaceAll(d, "fill_w", std::to_string(fillW));
        ReplaceAll(d, "glow_x", std::to_string(glowX));
        ReplaceAll(d, "glow_w", std::to_string(glowW));
        ReplaceAll(d, "detail_x", std::to_string(std::max(10, (guiW - 300) / 2)));
        ReplaceAll(d, "detail_y", std::to_string(barY + 12));
        ReplaceAll(d, "detail_w", std::to_string(std::min(300, guiW - 20)));
        ReplaceAll(d, "title", EscapeRml(t.title));
        ReplaceAll(d, "message", EscapeRml(t.message));
        ReplaceAll(d, "detail", EscapeRml(t.detail));
        return d;
    }
}
