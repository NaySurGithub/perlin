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
        if (Available(targetDir)) {

            const std::string path = "\"" + repo.string() + "\"";
            if (!fs::exists(repo / "resource_pack/models/blocks"))
                Run("git -c core.longpaths=true -C " + path + " sparse-checkout add resource_pack/models/blocks");

            if (!fs::exists(repo / "resource_pack/entity") || !fs::exists(repo / "behavior_pack/entities"))
                Run("git -c core.longpaths=true -C " + path + " sparse-checkout add resource_pack/entity behavior_pack/entities");

            if (!fs::exists(repo / "resource_pack/sounds/sound_definitions.json"))
                Run("git -c core.longpaths=true -C " + path + " sparse-checkout add resource_pack/sounds");
            if (!fs::exists(repo / "resource_pack/attachables"))
                Run("git -c core.longpaths=true -C " + path + " sparse-checkout add resource_pack/attachables resource_pack/animations "
                    "resource_pack/animation_controllers resource_pack/render_controllers resource_pack/models/entity");
            if (!fs::exists(repo / "resource_pack/ui/hud_screen.json") || !fs::exists(repo / "resource_pack/texts"))
                Run("git -c core.longpaths=true -C " + path + " sparse-checkout add resource_pack/ui resource_pack/texts resource_pack/font");
            return true;
        }

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
                                  "behavior_pack/blocks behavior_pack/biomes resource_pack/models/blocks "
                                  "resource_pack/ui resource_pack/texts resource_pack/font resource_pack/attachables "
                                  "resource_pack/animations resource_pack/animation_controllers resource_pack/render_controllers "
                                  "resource_pack/models/entity resource_pack/entity behavior_pack/entities resource_pack/sounds");
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
