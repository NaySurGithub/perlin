#include "client/ui/CreditsScreen.hpp"

#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiTemplate.hpp"

namespace perlin::ui {
    namespace {
        struct Line {
            const char* text;
            const char* cls;
        };
        // Blank entries are spacers.
        const Line kLines[] = {
            {"PERLIN", "title"},
            {"", ""},
            {"A Minecraft: Bedrock Edition client", "sub"},
            {"", ""},
            {"", ""},
            {"Made by", "head"},
            {"notchyves", "name"},
            {"", ""},
            {"", ""},
            {"Thanks to", "head"},
            {"Everyone who tested Perlin on their servers", "name"},
            {"and reported what broke", "name"},
            {"", ""},
            {"", ""},
            {"Minecraft is a trademark of Mojang AB.", "note"},
            {"Perlin is not affiliated with Mojang or Microsoft.", "note"},
            {"", ""},
            {"", ""},
            {"Thanks for playing!", "sub"},
        };
        constexpr int kLineHeight = 12;

        const char* kTemplate = R"PERLINRML(<rml>
<head>
<title>credits</title>
<style>
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background-color: #000000; }
#text { position: absolute; left: 0px; top: {{top}}px; width: {{w}}px; }
.l { display: block; height: 12px; line-height: 12px; text-align: center; font-family: minecraft-shadow; font-size: 8px; color: #ffffff; }
.title { color: #ffff55; font-size: 16px; height: 24px; line-height: 24px; }
.sub { color: #aaaaaa; }
.head { color: #ffff55; }
.note { color: #555555; }
.hint { position: absolute; left: 0px; top: {{hint_y}}px; width: {{w}}px; height: 12px; line-height: 12px; background-color: #000000; text-align: center; font-family: minecraft-shadow; font-size: 8px; color: #555555; }
</style>
</head>
<body>
  <div id="text">{{lines}}</div>
  <div class="hint">{{hint}}</div>
</body>
</rml>
)PERLINRML";
    }

    int CreditsHeight() {
        int h = 0;
        for (const Line& l : kLines)
            h += std::string(l.cls) == "title" ? 24 : kLineHeight;
        return h;
    }

    std::string BuildCreditsScreenRml(int guiW, int guiH, int scroll) {
        std::string lines;
        for (const Line& l : kLines)
            lines += std::string("<div class=\"l ") + l.cls + "\">" + (*l.text ? EscapeRml(l.text) : "&nbsp;") + "</div>";
        std::string d = kTemplate;
        ReplaceAll(d, "w", std::to_string(guiW));
        ReplaceAll(d, "h", std::to_string(guiH));
        ReplaceAll(d, "top", std::to_string(guiH - scroll));
        ReplaceAll(d, "hint_y", std::to_string(guiH - 12));
        ReplaceAll(d, "hint", "Press Escape to skip");
        ReplaceAll(d, "lines", lines);
        return d;
    }
}
