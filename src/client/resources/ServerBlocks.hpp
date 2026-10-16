#pragma once
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace perlin {
    struct CustomBlockDef {
        std::string name;
        std::map<std::string, std::string> faceTexture;
        bool cutout = false;
    };

    class ServerBlocks {
    public:
        static ServerBlocks& Instance();
        void Add(CustomBlockDef def);
        std::vector<CustomBlockDef> Take();
        bool Pending() const;

    private:
        mutable std::mutex mutex;
        std::vector<CustomBlockDef> pending;
    };
}
