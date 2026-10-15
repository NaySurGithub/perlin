#include "client/ui/UiTemplate.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace perlin::ui {
    namespace fs = std::filesystem;

    void ReplaceAll(std::string& text, const std::string& key, const std::string& value) {
        const std::string needle = "{{" + key + "}}";
        for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + value.size()))
            text.replace(at, needle.size(), value);
    }

    std::string LoadUiTemplate(const std::string& dataDir, const std::string& fileName, const std::string& defaultText,
                               uint64_t& version) {
        const fs::path file = fs::path(dataDir) / "ui" / fileName;
        std::error_code ec;
        bool stale = false;
        if (const size_t m = defaultText.find("<!--perlin-ui:"); m != std::string::npos && fs::exists(file, ec)) {
            const std::string marker = defaultText.substr(m, defaultText.find("-->", m) + 3 - m);
            std::ifstream existing(file, std::ios::binary);
            std::stringstream es;
            es << existing.rdbuf();
            stale = es.str().find(marker) == std::string::npos;
            if (stale) {
                fs::copy_file(file, fs::path(file).replace_extension(".rml.old"), fs::copy_options::overwrite_existing, ec);
                fs::remove(file, ec);
            }
        }
        if (!fs::exists(file, ec) || fs::file_size(file, ec) == 0) {
            fs::create_directories(file.parent_path(), ec);
            std::ofstream out(file, std::ios::binary);
            out << defaultText;
        }
        const auto stamp = fs::last_write_time(file, ec);
        const uint64_t v = ec ? 0 : static_cast<uint64_t>(stamp.time_since_epoch().count());
        std::ifstream in(file, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        if (!in || ss.str().empty()) {
            version = 0;
            return defaultText;
        }
        version = v;
        return ss.str();
    }
}
