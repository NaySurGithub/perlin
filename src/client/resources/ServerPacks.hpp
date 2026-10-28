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
        // Packs a server sent, not counting pinned test folders (which Clear keeps).
        bool HasServerPacks() const {
            std::lock_guard lock(mutex);
            return dirs.size() > pinned.size();
        }
        // Dev/test: use an already extracted pack folder as if the server had sent it.
        void AddDirectory(const std::string& directory);
        uint32_t Generation() const { return generation; }

    private:
        mutable std::mutex mutex;
        std::string root;
        std::vector<std::string> dirs;
        std::vector<std::string> pinned;
        std::atomic<uint32_t> generation{0};
    };
}
