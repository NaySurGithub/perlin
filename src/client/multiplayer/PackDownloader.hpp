#pragma once
#include "protocol/Packets.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace perlin {

    class PackDownloader {
    public:
        void SetDirectory(std::string dir) { directory = std::move(dir); }
        void Reset();

        std::vector<std::string> Plan(const std::vector<proto::TexturePackInfosEntry>& offered);

        enum class Step { Ignore, RequestChunk, PackDone, Failed };
        struct Result {
            Step step = Step::Ignore;
            std::string packId;
            uint32_t chunkIndex = 0;
            std::string message;
        };
        Result OnDataInfo(const proto::ResourcePackDataInfoPacket& info);
        Result OnChunk(const proto::ResourcePackChunkDataPacket& chunk);

        bool AllReceived() const { return awaiting.empty() && active.empty(); }
        size_t Downloaded() const { return downloaded; }
        size_t Cached() const { return cached; }

        static std::string UuidString(const net::Uuid& id);

    private:
        struct Active {
            std::string packId;
            uint32_t chunkCount = 0;
            uint32_t next = 0;
            uint64_t size = 0;
            std::vector<uint8_t> hash;
            std::vector<uint8_t> data;
            std::string fileName;
        };
        std::string directory;
        std::set<std::string> awaiting;
        std::map<std::string, std::string> fileFor;
        std::map<std::string, Active> active;
        size_t downloaded = 0, cached = 0;

        bool Store(const std::string& fileName, const std::vector<uint8_t>& bytes);
        bool Have(const std::string& fileName, uint64_t size) const;
    };

}
