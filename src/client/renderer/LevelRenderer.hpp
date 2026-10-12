#pragma once
#include "client/Options.hpp"
#include "client/renderer/Frustum.hpp"
#include "client/renderer/RenderContext.hpp"
#include "client/renderer/Tessellator.hpp"
#include "client/renderer/block/BiomeTints.hpp"
#include "client/renderer/block/BlockLibrary.hpp"
#include "util/ThreadPool.hpp"
#include "world/level/Level.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace perlin {
    class LevelRenderer {
    public:
        LevelRenderer(RenderContext& context, const Options& options);
        ~LevelRenderer();

        void Stop();

        void ReloadAssetsIfNeeded();
        void Update(Level* level, float camX, float camZ, const Frustum& frustum);
        void Render(const Frustum& frustum, float camX, float camZ);
        void TickAnimation(uint64_t step);

        const BlockLibrary& Blocks() const { return blocks; }
        const BiomeTints& Tints() const { return biomeTints; }
        size_t MeshedChunks() const { return meshVersions.size(); }
        bool TexturesLoaded() const { return atlasUploaded; }

    private:
        struct MeshResult {
            ChunkPos pos;
            uint64_t version;
            uint32_t generation;
            std::unique_ptr<Tessellator> mesh;
        };

        struct Pending {
            float distance;
            ChunkPos pos;
            uint64_t version;
        };

        struct Quiet {
            uint64_t version = 0;
            int frames = 0;
        };

        void UploadFinishedMeshes();
        void SnapshotVersions(Level& level);
        void CollectPending(float camX, float camZ, const Frustum& frustum);
        void ScheduleAsync(Level& level, size_t freeSlots);
        void MeshInline(Level& level);
        void InvalidateMeshes();
        void ConfigureLighting(const std::string& packDir);

        RenderContext& context;
        const Options& options;
        BlockLibrary blocks;
        BiomeTints biomeTints;

        std::unique_ptr<ThreadPool> meshPool;
        std::atomic<bool> stopping{false};
        std::atomic<uint32_t> generation{0};

        std::mutex doneMutex;
        std::vector<MeshResult> done;
        std::unordered_set<ChunkPos, ChunkPosHash> inFlight;
        std::unordered_map<ChunkPos, uint64_t, ChunkPosHash> meshVersions;
        std::unordered_map<ChunkPos, Quiet, ChunkPosHash> quiet;

        std::vector<std::pair<ChunkPos, uint64_t>> snapshot;
        std::vector<Pending> pending;

        const Level* lastLevel = nullptr;
        uint64_t lastChange = ~0ull;
        bool rescan = true;

        bool lightingConfigured = false;
        bool atlasUploaded = false;
        bool skyTexturesLoaded = false;
        void LoadSkyTextures();
        bool tintsWereReady = false;
        int uploadedTiles = 0;
        std::chrono::steady_clock::time_point lastAssetAttempt{};
    };
}
