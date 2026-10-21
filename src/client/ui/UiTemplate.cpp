#include "client/ui/UiTemplate.hpp"

#include <cinttypes>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace perlin::ui {
    namespace fs = std::filesystem;

    void ReplaceAll(std::string& text, const std::string& key, const std::string& value) {
        const std::string needle = "{{" + key + "}}";
        for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + value.size()))
            text.replace(at, needle.size(), value);
    }

    namespace {
        constexpr const char* kHeaderPrefix = "<!--perlin-ui-src:";

        uint64_t Fnv1a(const std::string& s) {
            uint64_t h = 1469598103934665603ull;
            for (const unsigned char c : s)
                h = (h ^ c) * 1099511628211ull;
            return h;
        }

        std::string Header(uint64_t hash) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s%016" PRIx64 "-->\n", kHeaderPrefix, hash);
            return buf;
        }

        bool ReadFile(const fs::path& file, std::string& out) {
            std::ifstream in(file, std::ios::binary);
            if (!in)
                return false;
            std::stringstream ss;
            ss << in.rdbuf();
            out = ss.str();
            return true;
        }

        bool SplitHeader(const std::string& content, uint64_t& hash, std::string& body) {
            const size_t prefix = std::char_traits<char>::length(kHeaderPrefix);
            if (content.compare(0, prefix, kHeaderPrefix) != 0)
                return false;
            const size_t nl = content.find('\n');
            if (nl == std::string::npos)
                return false;
            hash = std::strtoull(content.c_str() + prefix, nullptr, 16);
            body = content.substr(nl + 1);
            return true;
        }
    }

    std::string LoadUiTemplate(const std::string& dataDir, const std::string& fileName, const std::string& defaultText,
                               uint64_t& version) {
        const fs::path file = fs::path(dataDir) / "ui" / fileName;
        const uint64_t builtIn = Fnv1a(defaultText);
        std::error_code ec;

        bool rewrite = true;
        std::string content, body;
        if (fs::exists(file, ec) && ReadFile(file, content) && !content.empty()) {
            uint64_t fileHash = 0;
            if (SplitHeader(content, fileHash, body)) {
                if (fileHash == builtIn) {
                    rewrite = false;
                } else if (Fnv1a(body) != fileHash) {
                    fs::copy_file(file, fs::path(file).concat(".old"), fs::copy_options::overwrite_existing, ec);
                    std::cout << "[ui] " << fileName << " changed in this build; your edited copy was saved as "
                              << fileName << ".old" << std::endl;
                }
            } else if (content != defaultText) {
                fs::copy_file(file, fs::path(file).concat(".old"), fs::copy_options::overwrite_existing, ec);
            }
        }

        if (rewrite) {
            fs::create_directories(file.parent_path(), ec);
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << Header(builtIn) << defaultText;
            out.close();
            body = defaultText;
        }

        const auto stamp = fs::last_write_time(file, ec);
        if (ec || body.empty()) {
            version = 0;
            return defaultText;
        }
        version = static_cast<uint64_t>(stamp.time_since_epoch().count());
        return body;
    }
}
