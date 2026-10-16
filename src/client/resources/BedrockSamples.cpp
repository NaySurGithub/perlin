#include "client/resources/BedrockSamples.hpp"
#include "client/resources/SampleDownload.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace perlin {
    namespace {
        bool Complete(const fs::path& repo) {
            return fs::exists(repo / "resource_pack/blocks.json") &&
                   fs::exists(repo / "resource_pack/textures/terrain_texture.json") &&
                   fs::exists(repo / "metadata/vanilladata_modules/mojang-blocks.json") &&
                   fs::exists(repo / "behavior_pack/blocks");
        }

        constexpr const char* kMarker = ".ready3";
    }

    bool BedrockSamples::Available(const std::string& targetDir) {
        return fs::exists(fs::path(targetDir) / kMarker) && Complete(fs::path(targetDir) / "repo");
    }

    bool BedrockSamples::Ensure(const std::string& targetDir) {
#ifdef __EMSCRIPTEN__
        return Available(targetDir);
#else
        if (Available(targetDir))
            return true;
        for (int attempt = 0; attempt < 3; attempt++) {
            if (DownloadSamples(targetDir) && Complete(fs::path(targetDir) / "repo")) {
                std::ofstream((fs::path(targetDir) / kMarker).string()).close();
                std::cout << "[samples] ready at " << (fs::path(targetDir) / "repo").string() << std::endl;
                return true;
            }
            std::cout << "[samples] download incomplete (attempt " << attempt + 1 << ")" << std::endl;
        }
        return false;
#endif
    }
}
