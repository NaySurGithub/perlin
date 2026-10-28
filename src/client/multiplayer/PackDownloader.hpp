#pragma once
#include "protocol/Packets.hpp"

#include <atomic>
#include <chrono>
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
            // Chunks of packId to request now; several stay in flight to hide the round trip.
            std::vector<uint32_t> chunks;
            std::string message;
            // Set on PackDone when the next queued pack starts; chunks then belong to it.
            std::string nextPackId;
        };
        Result Tick();
        Result OnDataInfo(const proto::ResourcePackDataInfoPacket& info);
        Result OnChunk(const proto::ResourcePackChunkDataPacket& chunk);

        void MountAll();
        struct PackFile {
            std::string path, fileName, uuid, key;
        };
        // Pack zips of this session that exist on disk (used to store them in replays).
        std::vector<PackFile> Files() const;
        bool AllReceived() const { return awaiting.empty() && active.empty(); }
        size_t Downloaded() const { return downloaded; }
        struct Progress {
            size_t total = 0, done = 0;
            uint64_t bytes = 0, bytesTotal = 0;
        };
        Progress GetProgress() const {
            return {totalPacks.load(), donePacks.load(), receivedBytes.load(), expectedBytes.load()};
        }
        size_t Cached() const { return cached; }

        static std::string UuidString(const net::Uuid& id);

    private:
        struct Active {
            std::string packId;
            uint32_t chunkCount = 0;
            uint32_t next = 0;
            uint32_t requested = 0;
            uint64_t size = 0;
            std::vector<uint8_t> hash;
            std::vector<uint8_t> data;
            std::string fileName;
        };
        struct Offered {
            std::string fileName, uuid, key;
        };
        std::vector<Offered> offeredPacks;
        std::string currentKey;
        std::vector<std::string> queuedKeys;
        std::chrono::steady_clock::time_point requestedAt{};
        bool stallReported = false;
        static constexpr uint32_t kWindow = 8;
        // Requests the next chunks of the current pack until kWindow are outstanding.
        void Refill(Active& a, Result& r);
        std::atomic<size_t> totalPacks{0}, donePacks{0};
        std::atomic<uint64_t> receivedBytes{0}, expectedBytes{0};
        std::string directory;
        std::set<std::string> awaiting;
        std::map<std::string, std::string> fileFor;
        std::map<std::string, Active> active;
        size_t downloaded = 0, cached = 0;

        bool Store(const std::string& fileName, const std::vector<uint8_t>& bytes);
        bool Have(const std::string& fileName, uint64_t size) const;
    };

}
