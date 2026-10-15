#include "client/resources/SampleDownload.hpp"

#ifdef __ANDROID__
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <glaze/glaze.hpp>
#include <httplib.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {
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

    bool FetchAll(const Manifest& manifest, const std::vector<std::string>& files, const fs::path& repo, const char* label) {
        std::atomic<size_t> next{0}, done{0}, failed{0};
        const size_t total = files.size();
        const std::string prefix = manifest.baseUrl.empty()
                                       ? "/" + manifest.repository + "/" + manifest.ref + "/"
                                       : manifest.baseUrl;
        auto worker = [&] {
            httplib::SSLClient cli("raw.githubusercontent.com", 443);
            cli.set_ca_cert_path(std::string(), "/system/etc/security/cacerts");
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
}

namespace perlin {
    bool DownloadSamples(const std::string& targetDir) {
        size_t size = 0;
        void* data = SDL_LoadFile("assets-manifest.json", &size);
        if (!data) {
            std::cout << "[samples] assets-manifest.json is missing from the app" << std::endl;
            return false;
        }
        Manifest manifest;
        const std::string text(static_cast<const char*>(data), size);
        SDL_free(data);
        if (glz::read<glz::opts{.error_on_unknown_keys = false}>(manifest, text)) {
            std::cout << "[samples] assets-manifest.json is malformed" << std::endl;
            return false;
        }
        const fs::path root(targetDir);
        const fs::path repo = root / "repo";
        std::error_code ec;
        fs::create_directories(repo, ec);
        std::cout << "[samples] downloading " << manifest.files.core.size() << " files at " << manifest.ref << std::endl;
        if (!FetchAll(manifest, manifest.files.core, repo, "core"))
            return false;
        std::ofstream((root / ".ready").string()).close();
        FetchAll(manifest, manifest.files.sounds, repo, "sounds");
        return true;
    }
}
#endif
