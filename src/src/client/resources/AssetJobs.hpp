#pragma once
// Live status of the first-start asset downloads, for the download screen.
#include "network/FastDownload.hpp"

#include <atomic>
#include <cstdint>

namespace perlin::assets {
    enum class Stage : int { Idle, Checking, Downloading, Extracting, Done, Failed };

    struct Job {
        std::atomic<Stage> stage{Stage::Idle};
        download::Progress bytes;          // while downloading
        std::atomic<uint64_t> filesDone{0}; // while extracting
        std::atomic<uint64_t> filesTotal{0};
    };

    // Bedrock vanilla resources (textures, models, sounds...).
    inline Job& Samples() {
        static Job job;
        return job;
    }

    // The Java client jar (fonts and menu sprites).
    inline Job& Client() {
        static Job job;
        return job;
    }

    inline bool Busy(const Job& job) {
        const Stage s = job.stage.load();
        return s == Stage::Checking || s == Stage::Downloading || s == Stage::Extracting;
    }
}
