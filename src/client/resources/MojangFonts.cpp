#include "client/resources/MojangFonts.hpp"

#include <miniz.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "client/ui/Json.hpp"

namespace fs = std::filesystem;

namespace perlin {
    namespace {
        constexpr const char* kManifest = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
        constexpr const char* kResources = "https://resources.download.minecraft.net/";

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
        bool Run(const std::string& command) {
#ifdef _WIN32
            return std::system(("\"" + command + "\"").c_str()) == 0;
#else
            return std::system(command.c_str()) == 0;
#endif
        }

        bool Download(const std::string& url, const fs::path& target) {
            std::error_code ec;
            fs::create_directories(target.parent_path(), ec);
            const fs::path part = target.string() + ".part";
            for (int attempt = 0; attempt < 3; attempt++) {
                if (Run("curl -fsSL --retry 2 --connect-timeout 20 -o \"" + part.string() + "\" \"" + url + "\"")) {
                    fs::rename(part, target, ec);
                    if (!ec)
                        return true;
                }
            }
            fs::remove(part, ec);
            return false;
        }

        bool ReadFile(const fs::path& path, std::string& out) {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            std::ostringstream ss;
            ss << in.rdbuf();
            out = ss.str();
            return true;
        }

        bool ReadJson(const std::string& url, const fs::path& scratch, ui::Json& out) {
            std::string text, error;
            return Download(url, scratch) && ReadFile(scratch, text) && ui::Json::Parse(text, out, error);
        }

        bool ExtractHexFromZipMemory(const void* data, size_t size, const fs::path& hexOut) {
            mz_zip_archive zip{};
            if (!mz_zip_reader_init_mem(&zip, data, size, 0))
                return false;
            bool ok = false;
            const mz_uint count = mz_zip_reader_get_num_files(&zip);
            for (mz_uint i = 0; i < count && !ok; i++) {
                char name[512];
                if (!mz_zip_reader_get_filename(&zip, i, name, sizeof(name)))
                    continue;
                const std::string n = name;
                if (n.size() > 4 && n.compare(n.size() - 4, 4, ".hex") == 0) {
                    size_t outSize = 0;
                    void* bytes = mz_zip_reader_extract_to_heap(&zip, i, &outSize, 0);
                    if (bytes) {
                        std::error_code ec;
                        fs::create_directories(hexOut.parent_path(), ec);
                        std::ofstream out(hexOut, std::ios::binary);
                        out.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(outSize));
                        ok = out.good();
                        mz_free(bytes);
                    }
                }
            }
            mz_zip_reader_end(&zip);
            return ok;
        }

        bool ExtractFromJar(const fs::path& jar, const fs::path& hexOut, const fs::path& asciiOut) {
            mz_zip_archive zip{};
            if (!mz_zip_reader_init_file(&zip, jar.string().c_str(), 0))
                return false;
            bool hexOk = false;
            const mz_uint count = mz_zip_reader_get_num_files(&zip);
            for (mz_uint i = 0; i < count; i++) {
                char name[512];
                if (!mz_zip_reader_get_filename(&zip, i, name, sizeof(name)))
                    continue;
                const std::string n = name;
                if (n == "assets/minecraft/textures/font/ascii.png") {
                    size_t size = 0;
                    void* bytes = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
                    if (bytes) {
                        std::error_code ec;
                        fs::create_directories(asciiOut.parent_path(), ec);
                        std::ofstream out(asciiOut, std::ios::binary);
                        out.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(size));
                        mz_free(bytes);
                    }
                } else if (!hexOk && n.find("font/unifont_all_no_pua") != std::string::npos && n.size() > 4 && n.compare(n.size() - 4, 4, ".zip") == 0) {
                    size_t size = 0;
                    void* bytes = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
                    if (bytes) {
                        hexOk = ExtractHexFromZipMemory(bytes, size, hexOut);
                        mz_free(bytes);
                    }
                }
            }
            mz_zip_reader_end(&zip);
            return hexOk;
        }
#endif
    }

    std::string MojangFonts::UnihexPath(const std::string& dataDir) {
        return (fs::path(dataDir) / "fonts" / "unifont.hex").string();
    }

    bool MojangFonts::Available(const std::string& dataDir) {
        std::error_code ec;
        return fs::exists(UnihexPath(dataDir), ec) && fs::file_size(UnihexPath(dataDir), ec) > 1024 && fs::exists(fs::path(dataDir) / "font" / "ascii.png", ec);
    }

    bool MojangFonts::Ensure(const std::string& dataDir) {
        if (Available(dataDir))
            return true;
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
        return false;
#else
        const fs::path root = fs::path(dataDir) / "fonts";
        const fs::path hexOut = UnihexPath(dataDir);
        std::error_code ec;
        fs::create_directories(root, ec);
        ui::Json manifest;
        if (!ReadJson(kManifest, root / "manifest.tmp", manifest)) {
            std::cout << "[fonts] could not reach the Mojang version manifest" << std::endl;
            return false;
        }
        std::string release;
        if (const ui::Json* latest = manifest.Find("latest"))
            if (const ui::Json* r = latest->Find("release"))
                release = r->StringOr("");
        std::string versionUrl;
        if (const ui::Json* versions = manifest.Find("versions"); versions && versions->IsArray())
            for (const ui::Json& v : versions->array) {
                const ui::Json* id = v.Find("id");
                const ui::Json* url = v.Find("url");
                if (id && url && id->StringOr("") == release) {
                    versionUrl = url->StringOr("");
                    break;
                }
            }
        fs::remove(root / "manifest.tmp", ec);
        ui::Json version;
        if (versionUrl.empty() || !ReadJson(versionUrl, root / "version.tmp", version)) {
            std::cout << "[fonts] could not read the Mojang version metadata" << std::endl;
            return false;
        }
        fs::remove(root / "version.tmp", ec);
        std::string indexUrl, clientUrl;
        if (const ui::Json* index = version.Find("assetIndex"))
            if (const ui::Json* u = index->Find("url"))
                indexUrl = u->StringOr("");
        if (const ui::Json* downloads = version.Find("downloads"))
            if (const ui::Json* client = downloads->Find("client"))
                if (const ui::Json* u = client->Find("url"))
                    clientUrl = u->StringOr("");
        bool ok = false;
        const fs::path asciiOut = fs::path(dataDir) / "font" / "ascii.png";
        if (!clientUrl.empty()) {
            const fs::path jar = root / "client.tmp";
            if (Download(clientUrl, jar))
                ok = ExtractFromJar(jar, hexOut, asciiOut);
            fs::remove(jar, ec);
        }
        ui::Json assets;
        if (!ok && !indexUrl.empty() && ReadJson(indexUrl, root / "assets.tmp", assets)) {
            if (const ui::Json* objects = assets.Find("objects"); objects && objects->IsObject())
                for (const auto& [key, value] : objects->object) {
                    if (key.find("font/unifont_all_no_pua") == std::string::npos || key.size() < 4 || key.compare(key.size() - 4, 4, ".zip") != 0)
                        continue;
                    const ui::Json* hash = value.Find("hash");
                    if (!hash || hash->StringOr("").size() < 2)
                        continue;
                    const std::string h = hash->StringOr("");
                    const fs::path zip = root / "unifont.zip";
                    std::string bytes;
                    if (Download(std::string(kResources) + h.substr(0, 2) + "/" + h, zip) && ReadFile(zip, bytes))
                        ok = ExtractHexFromZipMemory(bytes.data(), bytes.size(), hexOut);
                    fs::remove(zip, ec);
                    if (ok)
                        break;
                }
        }
        fs::remove(root / "assets.tmp", ec);
        std::cout << (ok ? "[fonts] Unicode font ready" : "[fonts] Unicode font download failed") << std::endl;
        return ok;
#endif
    }
}
