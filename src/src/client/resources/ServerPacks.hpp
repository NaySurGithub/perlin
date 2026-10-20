#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace perlin {
    class ServerPacks {
    public:
        static ServerPacks& Instance();
        void SetRoot(const std::string& directory);
        void Clear();
        bool Mount(const std::string& zipPath, const std::string& id, const std::string& contentKey);
        std::vector<std::string> Directories() const;
        uint32_t Generation() const { return generation; }

    private:
        mutable std::mutex mutex;
        std::string root;
        std::vector<std::string> dirs;
        std::atomic<uint32_t> generation{0};
    };
}
