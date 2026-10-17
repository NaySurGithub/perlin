#pragma once
// Fast file downloads: the file is fetched as several byte ranges over parallel HTTPS connections
// (when the server supports ranges, as GitHub release assets and Mojang's CDN do), written
// straight into place, with live progress for the UI.
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

namespace perlin::download {
    struct Progress {
        std::atomic<uint64_t> done{0};
        std::atomic<uint64_t> total{0}; // 0 until known
        std::atomic<bool> cancel{false};
    };

    struct Options {
        int connections = 8;
        uint64_t chunkBytes = 4ull << 20;
        int timeoutSecs = 30;
    };

    // Downloads url to target (via target + ".part"). Returns false on failure; the partial file
    // is removed.
    bool Fetch(const std::string& url, const std::filesystem::path& target, Progress* progress = nullptr, const Options& options = {});

    // Follows redirects without downloading; returns the final URL ("" on failure). Handy for
    // GitHub's /releases/latest, which redirects to /releases/tag/<tag>.
    std::string Resolve(const std::string& url, int timeoutSecs = 20);
}
