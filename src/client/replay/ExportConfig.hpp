#pragma once
#include <string>
#include <vector>

#include "client/replay/Ffmpeg.hpp"

namespace perlin::replay {
    struct Resolution {
        const char* label;
        int w, h;
    };
    const std::vector<Resolution>& ResolutionPresets();
    struct AspectRatio {
        const char* label;
        int w, h; // 0,0 = free
    };
    const std::vector<AspectRatio>& AspectRatios();
    const std::vector<double>& FpsPresets();

    enum class RangeMode { Whole = 0, InOut = 1, Custom = 2 };

    struct ExportConfig {
        std::string container = "mp4";
        std::string codec = "h264";
        int width = 1920, height = 1080;
        int aspectIdx = 0;       // index into AspectRatios()
        double fps = 60.0;
        int crf = 20;
        int bitrateKbps = 0;
        int ssaa = 1;            // 1..4
        bool hideGui = true;
        bool transparent = false;
        RangeMode range = RangeMode::Whole;
        double customStartSec = 0, customEndSec = 10;
        std::string outputDir;      // "" = <DataDir>/replays/exports
        std::string nameTemplate = "{replay}_{date}";
        std::string preset, pixelFormat, extraArgs, encoderOverride, muxerOverride, extensionOverride;
        int gifColors = 256;

        std::string ToJson() const;
        static ExportConfig FromJson(const std::string& text);
    };

    // Expands {replay} {date} {time} {res} {fps} {container}; strips characters invalid in file names.
    std::string ExpandNameTemplate(const std::string& tpl, const std::string& replayName, int w, int h, double fps, const std::string& container);
    std::string ExportDirectory(const ExportConfig& c);
    std::string FpsLabel(double fps);
    // Parses "29.97", "30000/1001", "60". Returns <=0 on failure.
    double ParseFps(const std::string& text);
    // Returns a path that doesn't exist yet by appending " (2)" etc.
    std::string UniquePath(const std::string& path, bool isDirectory);
}
