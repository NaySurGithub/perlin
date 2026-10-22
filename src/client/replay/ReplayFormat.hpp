#pragma once
// .perlinreplay: a zip archive holding
//   metadata.json     - Metadata (below)
//   chunk_N.bin       - consecutive blocks of Records ordered by time (the first block starts with the world snapshot at t=0)
//   thumbnail.png     - optional preview
//   editor.json       - optional timeline / keyframes / export presets saved by the replay editor
// A Record is  u8 dir | u32 timeMs | u32 packetId | u32 length | body   (little endian). dir 0 = server->client, 1 = client->server.
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "client/ui/Json.hpp"

namespace perlin::replay {
    inline constexpr int kFormatVersion = 1;
    inline constexpr const char* kExtension = ".perlinreplay";

    // Snapshot records (dir 2) are periodic full-state copies written so seeking does not have to replay from t=0. They are
    // skipped during normal playback and only injected when playback restarts from that point.
    enum class Dir : uint8_t { ToClient = 0, ToServer = 1, Snapshot = 2 };

    struct Record {
        Dir dir = Dir::ToClient;
        uint32_t timeMs = 0;
        uint32_t id = 0;
        std::vector<uint8_t> body;
    };

    struct Marker {
        uint32_t timeMs = 0;
        std::string name;
        uint32_t color = 0xFFFFFF;
    };

    struct ChunkInfo {
        std::string file;
        uint32_t startMs = 0, endMs = 0, records = 0;
        bool snapshot = false;  // starts with Dir::Snapshot records
    };

    struct Pose {
        float x = 0, y = 0, z = 0, yaw = 0, pitch = 0;
        bool known = false;
    };

    struct Metadata {
        int version = kFormatVersion;
        std::string name, server, gameVersion, playerName, perlinVersion;
        int protocol = 0;
        int64_t createdUnix = 0;
        uint32_t durationMs = 0;
        uint64_t localRuntimeId = 0;
        Pose startPose;
        std::vector<ChunkInfo> chunks;
        std::vector<Marker> markers;
        ui::Json ToJson() const;
        static bool FromJson(const ui::Json& j, Metadata& out, std::string& error);
    };

    void EncodeRecord(const Record& r, std::vector<uint8_t>& out);
    // Returns bytes consumed or 0 when the buffer is truncated.
    size_t DecodeRecord(const uint8_t* data, size_t size, Record& out);

    // Packets that must never be replayed (handshake / resource-pack negotiation / disconnects).
    bool IsHandshakePacket(uint32_t id);

    // Keeps enough of the live session to reconstruct the world when a recording starts mid-game.
    class StateCache {
    public:
        void Reset();
        void OnToClient(uint32_t id, const std::vector<uint8_t>& body);
        // Packets, in an order that rebuilds the state (all timeMs = 0).
        std::vector<Record> Snapshot() const;
        size_t Bytes() const;

    private:
        struct Entry {
            uint64_t seq = 0;
            uint32_t id = 0;
            std::vector<uint8_t> body;
            uint64_t unique = 0, runtime = 0;
        };
        mutable std::mutex mutex;
        uint64_t seq = 0;
        bool joined = false;
        size_t chunkBytes = 0, joinBytes = 0, listBytes = 0;
        std::vector<Entry> join;                       // everything before the player spawned
        std::map<uint64_t, Entry> chunks;              // LevelChunk by (x,z,dim) key / SubChunk by body hash
        std::vector<Entry> blockUpdates;               // bounded FIFO
        std::vector<Entry> playerLists;                // bounded FIFO
        std::map<uint64_t, Entry> entities;            // AddActor/AddPlayer/AddItemActor by runtime id
        std::map<std::pair<uint32_t, uint64_t>, Entry> latest;  // per-entity / per-container latest packets
        std::map<uint32_t, Entry> singletons;          // latest packet of an id
        void DropEntity(uint64_t id);
    };

    class Recorder {
    public:
        ~Recorder();
        using SnapshotProvider = std::function<std::vector<Record>()>;
        // `periodicMs` > 0 writes a fresh snapshot (via `provider`) that often, unless `maxSnapshotBytes` would be exceeded.
        bool Start(const std::string& workDir, Metadata meta, const std::vector<Record>& snapshot, std::string& error,
                   SnapshotProvider provider = nullptr, uint32_t periodicMs = 0, size_t maxSnapshotBytes = 0);
        void Push(Dir dir, uint32_t id, const std::vector<uint8_t>& body);
        void AddMarker(const std::string& name);
        // Writes `outPath` (a .perlinreplay). `thumbnailPng` may be empty.
        bool Stop(const std::string& outPath, const std::vector<uint8_t>& thumbnailPng, std::string& error);
        void Abort();
        bool Active() const { return active; }
        uint32_t ElapsedMs() const;
        uint64_t BytesWritten() const { return bytesTotal; }

    private:
        void FlushLocked();
        mutable std::mutex mutex;
        bool active = false;
        std::string dir;
        Metadata meta;
        std::vector<uint8_t> buffer;
        uint32_t bufferStartMs = 0, lastMs = 0, bufferRecords = 0;
        int64_t startTick = 0;
        uint64_t bytesTotal = 0;
        std::vector<ChunkInfo> chunkInfos;
        SnapshotProvider provider;
        uint32_t periodicMs = 0, nextSnapshotMs = 0;
        size_t maxSnapshotBytes = 0;
        bool nextChunkSnapshot = false;
    };

    class Reader {
    public:
        bool Open(const std::string& path, std::string& error);
        // Quick metadata-only read (used by the replay list).
        static bool ReadMetadata(const std::string& path, Metadata& out, std::vector<uint8_t>* thumbnail, std::string& error);
        const Metadata& Meta() const { return meta; }
        bool LoadChunk(size_t index, std::vector<Record>& out, std::string& error);
        std::string ReadFile(const std::string& name);  // editor.json etc.
        const std::string& Path() const { return path; }

    private:
        std::string path;
        Metadata meta;
        std::shared_ptr<void> zip;
    };

    // Writes dir/chunk files listed in meta.chunks (plus metadata and an optional thumbnail) into a .perlinreplay.
    bool PackArchive(const std::string& dir, const Metadata& meta, const std::vector<uint8_t>& thumbnail, const std::string& outPath,
                     std::string& error);
    // Rewrites metadata.json / editor.json of an existing replay without touching packet data.
    bool UpdateArchiveFile(const std::string& path, const std::string& name, const std::string& content, std::string& error);
    std::string SafeFileName(const std::string& name);
    std::string ReplayDirectory();
}
