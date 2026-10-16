#include "client/resources/SampleDownload.hpp"

#ifndef __EMSCRIPTEN__
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <glaze/glaze.hpp>
#include <httplib.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include "client/ui/Json.hpp"
#include "network/Http.hpp"

namespace fs = std::filesystem;

namespace perlin::sample_manifest {
    struct ManifestFiles {
        std::vector<std::string> core;
        std::vector<std::string> sounds;
    };

    struct Manifest {
        std::string repository;
        std::string ref;
        std::string baseUrl;
        ManifestFiles files;
    };
}

namespace {
    using perlin::sample_manifest::Manifest;

    constexpr const char* kRepository = "Mojang/bedrock-samples";
    constexpr const char* kBranch = "main";

    const std::vector<std::string>& Directories() {
        static const std::vector<std::string> dirs = {
            "resource_pack/textures", "metadata/vanilladata_modules", "behavior_pack/blocks", "behavior_pack/biomes",
            "resource_pack/models", "resource_pack/ui", "resource_pack/texts", "resource_pack/font",
            "resource_pack/attachables", "resource_pack/animations", "resource_pack/animation_controllers",
            "resource_pack/render_controllers", "resource_pack/entity", "behavior_pack/entities",
            "resource_pack/sounds", "resource_pack/biomes",
        };
        return dirs;
    }

    std::string Encode(const std::string& path) {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        for (unsigned char c : path) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                c == '~' || c == '/') {
                out.push_back(static_cast<char>(c));
            } else {
                out.push_back('%');
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 15]);
            }
        }
        return out;
    }

    bool ListTree(const std::string& dir, bool recursive, std::vector<std::string>& files, std::vector<std::string>& subdirs, bool& truncated) {
        const std::string url = std::string("https://api.github.com/repos/") + kRepository + "/git/trees/" + kBranch + ":" + Encode(dir) +
                                (recursive ? "?recursive=1" : "");
        perlin::http::Response res;
        for (int attempt = 0; attempt < 3; attempt++) {
            res = perlin::http::GetUrl(url, {{"Accept", "application/vnd.github+json"}}, 60);
            if (res.status == 200)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(500 * (attempt + 1)));
        }
        if (res.status == 404)
            return true;
        if (res.status != 200) {
            std::cout << "[samples] listing " << dir << " failed (" << res.status << ")" << std::endl;
            return false;
        }
        perlin::ui::Json json;
        std::string error;
        if (!perlin::ui::Json::Parse(res.body, json, error))
            return false;
        truncated = false;
        if (const perlin::ui::Json* t = json.Find("truncated"))
            truncated = t->BoolOr(false);
        const perlin::ui::Json* tree = json.Find("tree");
        if (!tree || !tree->IsArray())
            return false;
        for (const perlin::ui::Json& entry : tree->array) {
            const perlin::ui::Json* path = entry.Find("path");
            const perlin::ui::Json* type = entry.Find("type");
            if (!path || !type)
                continue;
            const std::string full = dir + "/" + path->StringOr("");
            if (type->StringOr("") == "blob")
                files.push_back(full);
            else if (type->StringOr("") == "tree")
                subdirs.push_back(full);
        }
        return true;
    }

    bool CollectDirectory(const std::string& dir, std::vector<std::string>& files) {
        std::vector<std::string> subdirs;
        bool truncated = false;
        if (!ListTree(dir, true, files, subdirs, truncated))
            return false;
        if (!truncated)
            return true;
        files.clear();
        subdirs.clear();
        if (!ListTree(dir, false, files, subdirs, truncated))
            return false;
        for (const std::string& sub : subdirs)
            if (!CollectDirectory(sub, files))
                return false;
        return true;
    }

    bool FetchAll(const std::string& prefix, const std::vector<std::string>& files, const fs::path& repo, const char* label) {
        std::atomic<size_t> next{0}, done{0}, failed{0};
        const size_t total = files.size();
        auto worker = [&] {
            httplib::SSLClient cli("raw.githubusercontent.com", 443);
#ifdef __ANDROID__
            cli.set_ca_cert_path(std::string(), "/system/etc/security/cacerts");
#endif
            cli.set_connection_timeout(20);
            cli.set_read_timeout(60);
            cli.set_keep_alive(true);
            cli.set_follow_location(true);
            for (;;) {
                const size_t i = next.fetch_add(1);
                if (i >= total)
                    break;
                const fs::path target = repo / files[i];
                std::error_code ec;
                if (fs::exists(target, ec) && fs::file_size(target, ec) > 0) {
                    done++;
                    continue;
                }
                bool ok = false;
                for (int attempt = 0; attempt < 4 && !ok; attempt++) {
                    httplib::Result res = cli.Get(prefix + Encode(files[i]));
                    if (!res || res->status != 200) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(300 * (attempt + 1)));
                        continue;
                    }
                    fs::create_directories(target.parent_path(), ec);
                    const fs::path temp = target.string() + ".part";
                    {
                        std::ofstream out(temp, std::ios::binary);
                        out.write(res->body.data(), static_cast<std::streamsize>(res->body.size()));
                        if (!out.good())
                            continue;
                    }
                    fs::rename(temp, target, ec);
                    ok = !ec;
                }
                if (!ok)
                    failed++;
                const size_t n = ++done;
                if (n % 500 == 0)
                    std::cout << "[samples] " << label << " " << n << "/" << total << std::endl;
            }
        };
        std::vector<std::thread> threads;
        for (int i = 0; i < 12; i++)
            threads.emplace_back(worker);
        for (std::thread& t : threads)
            t.join();
        std::cout << "[samples] " << label << " done, " << failed.load() << " failed of " << total << std::endl;
        return failed.load() == 0;
    }

    bool DownloadFromManifest(const std::string& targetDir) {
        size_t size = 0;
        void* data = SDL_LoadFile("assets-manifest.json", &size);
        if (!data)
            return false;
        Manifest manifest;
        const std::string text(static_cast<const char*>(data), size);
        SDL_free(data);
        if (glz::read<glz::opts{.error_on_unknown_keys = false}>(manifest, text)) {
            std::cout << "[samples] assets-manifest.json is malformed" << std::endl;
            return false;
        }
        const std::string prefix = manifest.baseUrl.empty() ? "/" + manifest.repository + "/" + manifest.ref + "/" : manifest.baseUrl;
        const fs::path repo = fs::path(targetDir) / "repo";
        std::error_code ec;
        fs::create_directories(repo, ec);
        if (!FetchAll(prefix, manifest.files.core, repo, "core"))
            return false;
        FetchAll(prefix, manifest.files.sounds, repo, "sounds");
        return true;
    }
}

namespace perlin {
    bool DownloadSamples(const std::string& targetDir) {
#ifdef __ANDROID__
        return DownloadFromManifest(targetDir);
#else
        const fs::path repo = fs::path(targetDir) / "repo";
        std::error_code ec;
        fs::create_directories(repo, ec);
        std::vector<std::string> files;
        std::vector<std::string> subdirs;
        bool truncated = false;
        if (!ListTree("resource_pack", false, files, subdirs, truncated))
            return false;
        std::vector<std::string> roots;
        for (const std::string& f : files)
            roots.push_back(f);
        files = roots;
        for (const std::string& dir : Directories())
            if (!CollectDirectory(dir, files))
                return false;
        std::cout << "[samples] downloading " << files.size() << " files" << std::endl;
        const std::string prefix = std::string("/") + kRepository + "/" + kBranch + "/";
        return FetchAll(prefix, files, repo, "samples");
#endif
    }
}
#else
namespace perlin {
    bool DownloadSamples(const std::string&) { return false; }
}
#endif
