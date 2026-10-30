#pragma once
// reads the java edition worldgen data (data/minecraft/worldgen/...) from a client jar or an extracted folder
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace perlin::server::vanilla {

    class DataPack {
    public:
        ~DataPack();
        // a .jar / .zip, or a directory that holds data/minecraft/...
        static std::unique_ptr<DataPack> Open(const std::string& path, std::string& error);
        // "worldgen/noise/continentalness.json" (relative to data/minecraft/); empty when missing
        std::string Read(const std::string& relative) const;
        bool Has(const std::string& relative) const;
        std::vector<std::string> List(const std::string& relativePrefix) const;

    private:
        DataPack() = default;
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
