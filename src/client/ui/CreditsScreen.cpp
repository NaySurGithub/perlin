#include "client/ui/CreditsScreen.hpp"

#include "client/resources/ClientAssets.hpp"
#include "client/ui/Json.hpp"
#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiTemplate.hpp"
#include "util/Paths.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace perlin::ui {
    namespace {
        struct Line {
            std::string text;
            std::string cls;
            int Height() const { return cls == "title" ? 24 : 12; }
        };

        // Blank entries are spacers.
        std::vector<Line> PerlinLines() {
            return {
                {"PERLIN", "title"},
                {"", ""},
                {"A Minecraft: Bedrock Edition client", "sub"},
                {"", ""},
                {"", ""},
                {"Made by", "head"},
                {"notchyves", "name"},
                {"", ""},
                {"", ""},
                {"Perlin is not affiliated with, endorsed by or", "note"},
                {"associated with Mojang Studios or Microsoft.", "note"},
                {"Minecraft is a trademark of Mojang AB.", "note"},
                {"", ""},
                {"", ""},
            };
        }

        // Mojang's Java Edition credits, copied out of the client jar with the other client assets.
        bool AppendJavaCredits(std::vector<Line>& lines) {
            const std::filesystem::path file = std::filesystem::path(clientassets::Directory(paths::DataDir())) / "credits.json";
            std::ifstream in(file, std::ios::binary);
            if (!in)
                return false;
            std::stringstream ss;
            ss << in.rdbuf();
            Json root;
            std::string error;
            if (!Json::Parse(ss.str(), root, error) || !root.IsArray())
                return false;
            lines.push_back({"Minecraft: Java Edition", "head"});
            lines.push_back({"", ""});
            for (const Json& section : root.array) {
                if (const Json* name = section.Find("section"))
                    lines.push_back({name->StringOr(""), "head"});
                lines.push_back({"", ""});
                const Json* disciplines = section.Find("disciplines");
                if (!disciplines || !disciplines->IsArray())
                    continue;
                for (const Json& discipline : disciplines->array) {
                    if (const Json* name = discipline.Find("discipline"); name && !name->StringOr("").empty())
                        lines.push_back({name->StringOr(""), "head"});
                    const Json* titles = discipline.Find("titles");
                    if (!titles || !titles->IsArray())
                        continue;
                    for (const Json& title : titles->array) {
                        if (const Json* name = title.Find("title"); name && !name->StringOr("").empty())
                            lines.push_back({name->StringOr(""), "sub"});
                        if (const Json* names = title.Find("names"); names && names->IsArray())
                            for (const Json& person : names->array)
                                lines.push_back({person.StringOr(""), "name"});
                        lines.push_back({"", ""});
                    }
                }
                lines.push_back({"", ""});
            }
            return true;
        }

        const std::vector<Line>& Lines() {
            // The jar is extracted in the background on first launch, so keep looking until the Java list shows up.
            static std::vector<Line> lines;
            static bool java = false;
            if (lines.empty() || !java) {
                lines = PerlinLines();
                java = AppendJavaCredits(lines);
                lines.push_back({"", ""});
                lines.push_back({"Thanks for playing!", "sub"});
            }
            return lines;
        }

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
        for (const Line& l : Lines())
            h += l.Height();
        return h;
    }

    std::string BuildCreditsScreenRml(int guiW, int guiH, int scroll) {
        // The Java list runs to thousands of lines, so only the ones on screen go into the document.
        const int top = guiH - scroll;
        int y = 0, skipped = 0;
        std::string lines;
        for (const Line& l : Lines()) {
            const int h = l.Height();
            if (top + y + h < 0)
                skipped += h;
            else if (top + y <= guiH)
                lines += "<div class=\"l " + l.cls + "\">" + (l.text.empty() ? std::string("&nbsp;") : EscapeRml(l.text)) + "</div>";
            y += h;
        }
        std::string d = kTemplate;
        ReplaceAll(d, "w", std::to_string(guiW));
        ReplaceAll(d, "h", std::to_string(guiH));
        ReplaceAll(d, "top", std::to_string(top + skipped));
        ReplaceAll(d, "hint_y", std::to_string(guiH - 12));
        ReplaceAll(d, "hint", "Press Escape to skip");
        ReplaceAll(d, "lines", lines);
        return d;
    }
}
