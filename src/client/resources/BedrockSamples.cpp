#include "client/resources/BedrockSamples.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace perlin {
    std::string BedrockSamples::Ensure(const std::string& targetDir) {
        std::string marker = targetDir + "/.done";
        if (fs::exists(marker))
            return targetDir + "/Bedrock";

        fs::create_directories(targetDir);

        std::string cmd =
            "git clone --depth 1 https://github.com/Mojang/bedrock-samples \"" + targetDir + "/repo\"";
        if (std::system(cmd.c_str()) != 0)
            return {};

        std::string src = targetDir + "/repo/resource_pack";
        std::string dst = targetDir + "/Bedrock";
        if (fs::exists(src)) {
            std::error_code ec;
            fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        }

        std::ofstream(marker).close();
        return dst;
    }
}
