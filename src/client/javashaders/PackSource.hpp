#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace perlin::jshader {
    // Read-only view of a Java (OptiFine / Iris) shader pack. All paths are relative to the pack's "shaders" folder and
    // use '/' separators (no leading slash).
    class PackSource {
    public:
        virtual ~PackSource() = default;
        virtual bool Exists(const std::string& path) const = 0;
        virtual bool Read(const std::string& path, std::string& out) const = 0;
        // Files directly or indirectly under `dir` (empty = everything), as paths relative to the shaders folder.
        virtual std::vector<std::string> List(const std::string& dir) const = 0;
        virtual std::string Name() const = 0;
    };

    // A pack unpacked into a folder. `root` is the folder that contains "shaders/" (or is the shaders folder itself).
    std::unique_ptr<PackSource> OpenDirectoryPack(const std::string& root);
    // A .zip pack. Available when the build links miniz (see PackSourceZip.cpp).
    std::unique_ptr<PackSource> OpenZipPack(const std::string& zipPath);
    // Opens either kind based on what `path` is.
    std::unique_ptr<PackSource> OpenPack(const std::string& path);

    // True when `path` (folder or .zip) holds an OptiFine / Iris style pack.
    bool LooksLikeJavaPack(const std::string& path);
    // Iris keeps a pack's option choices in "<pack>.txt" next to it (key=value lines). Missing file = defaults.
    std::map<std::string, std::string> ReadJavaPackOptions(const std::string& path);

    std::string NormalizePath(const std::string& path);
    std::string DirectoryOf(const std::string& path);
    std::string JoinPath(const std::string& dir, const std::string& rel);
}
