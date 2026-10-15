#include "client/ui/DeathScreen.hpp"

#include "client/ui/RmlEmit.hpp"
#include "util/Resources.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace perlin::ui {
    namespace fs = std::filesystem;

    namespace {
        std::string StripCodes(const std::string& in) {
            std::string out;
            for (size_t i = 0; i < in.size(); i++) {
                if (static_cast<unsigned char>(in[i]) == 0xC2 && i + 2 < in.size() && static_cast<unsigned char>(in[i + 1]) == 0xA7) {
                    i += 2;
                    continue;
                }
                out += in[i];
            }
            return out;
        }

        void ReplaceAll(std::string& text, const std::string& key, const std::string& value) {
            const std::string needle = "{{" + key + "}}";
            for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + value.size()))
                text.replace(at, needle.size(), value);
        }
    }

    std::string DefaultDeathScreenTemplate() {
        std::string text = resources::ReadText("death_screen.rml");
        return text.empty() ? EmbeddedDeathScreenTemplate() : text;
    }

    std::string LoadDeathScreenTemplate(const std::string& dataDir, uint64_t& version) {
        const fs::path file = fs::path(dataDir) / "ui" / "death_screen.rml";
        std::error_code ec;
        bool stale = false;
        if (fs::exists(file, ec)) {
            std::ifstream probe(file, std::ios::binary);
            std::stringstream existing;
            existing << probe.rdbuf();
            stale = existing.str().find("class=\"strip\"") != std::string::npos;
        }
        if (stale || !fs::exists(file, ec) || fs::file_size(file, ec) == 0) {
            fs::create_directories(file.parent_path(), ec);
            std::ofstream out(file, std::ios::binary);
            out << DefaultDeathScreenTemplate();
        }
        const auto stamp = fs::last_write_time(file, ec);
        const uint64_t v = ec ? 0 : static_cast<uint64_t>(stamp.time_since_epoch().count());
        std::ifstream in(file, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        if (!in || ss.str().empty()) {
            version = 0;
            return DefaultDeathScreenTemplate();
        }
        version = v;
        return ss.str();
    }

    std::string BuildDeathScreenRml(const std::string& templateText, const DeathScreenText& t, int guiW, int guiH) {
        std::string d = templateText;
        ReplaceAll(d, "w", std::to_string(guiW));
        ReplaceAll(d, "h", std::to_string(guiH));
        ReplaceAll(d, "btn_x", std::to_string(guiW / 2 - 100));
        ReplaceAll(d, "btn1_y", std::to_string(guiH / 4 + 72));
        ReplaceAll(d, "btn2_y", std::to_string(guiH / 4 + 96));
        ReplaceAll(d, "off", t.buttonsEnabled ? "" : " off");
        ReplaceAll(d, "title_screen", EscapeRml(t.titleScreen));
        ReplaceAll(d, "title", EscapeRml(t.title));
        ReplaceAll(d, "cause", EscapeRml(StripCodes(t.cause)));
        ReplaceAll(d, "respawn", EscapeRml(t.respawn));
        return d;
    }
}
