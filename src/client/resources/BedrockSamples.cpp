#include "client/resources/BedrockSamples.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace perlin {
    namespace {
        constexpr const char* kRepository = "https://github.com/Mojang/bedrock-samples";

        bool Run(const std::string& command) {
#ifdef _WIN32
            return std::system(("\"" + command + "\"").c_str()) == 0;
#else
            return std::system(command.c_str()) == 0;
#endif
        }

        bool Complete(const fs::path& repo) {
            return fs::exists(repo / "resource_pack/blocks.json") &&
                   fs::exists(repo / "resource_pack/textures/terrain_texture.json") &&
                   fs::exists(repo / "metadata/vanilladata_modules/mojang-blocks.json") &&
                   fs::exists(repo / "behavior_pack/blocks");
        }
    }

    bool BedrockSamples::Available(const std::string& targetDir) {
        return fs::exists(fs::path(targetDir) / ".ready") && Complete(fs::path(targetDir) / "repo");
    }

    bool BedrockSamples::Ensure(const std::string& targetDir) {
        const fs::path root(targetDir);
        const fs::path repo = root / "repo";
        if (Available(targetDir))
            return true;

        std::error_code ec;
        fs::create_directories(root, ec);

        const std::string path = "\"" + repo.string() + "\"";
        const std::string git = "git -c core.longpaths=true ";
        for (int attempt = 0; attempt < 3; attempt++) {
            if (!Complete(repo)) {
                fs::remove_all(repo, ec);
                bool cloned = Run(git + "clone --depth 1 --filter=blob:none --sparse " + kRepository + " " + path) &&
                              Run(git + "-C " + path +
                                  " sparse-checkout set resource_pack/textures metadata/vanilladata_modules "
                                  "behavior_pack/blocks behavior_pack/biomes");
                if (!cloned) {
                    fs::remove_all(repo, ec);
                    cloned = Run(git + "clone --depth 1 " + kRepository + " " + path);
                }
                if (!cloned) {
                    std::cout << "[samples] clone failed (attempt " << attempt + 1 << "); is git installed?"
                              << std::endl;
                    continue;
                }
            }
            if (Complete(repo)) {
                std::ofstream((root / ".ready").string()).close();
                std::cout << "[samples] ready at " << repo.string() << std::endl;
                return true;
            }
        }
        return false;
    }
}
