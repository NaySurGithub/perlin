#include <cstdio>
#include "client/renderer/LevelRenderer.hpp"
#include "client/resources/BedrockSamples.hpp"
#include "client/resources/TextureLoader.hpp"
#include "client/resources/TexturePack.hpp"
#include <iostream>
#include "client/renderer/chunk/ChunkMesher.hpp"
#include "util/Paths.hpp"
#include "world/level/lighting/LightRegistry.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace perlin {
    namespace {
        constexpr size_t kMaxInFlight = 8;
        constexpr int kMinQuietFrames = 3;
        constexpr int kRemeshQuietFrames = 6;
        constexpr float kOffscreenPenalty = 1e9f;
        constexpr auto kInlineBudget = std::chrono::milliseconds(6);
        constexpr auto kAssetRetry = std::chrono::seconds(1);

        std::string ReadText(const std::string& path) {
            std::ifstream file(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(file)), {});
        }

        uint64_t Combine(uint64_t hash, uint64_t value) {
            return (hash ^ value) * 1099511628211ull;
        }
    }

    LevelRenderer::LevelRenderer(RenderContext& context, const Options& options)
        : context(context), options(options) {
        blocks.SetFancy(options.fancyGraphics);
        const unsigned cores = std::thread::hardware_concurrency();
        const unsigned threads = std::clamp(cores > 3 ? cores - 2 : 1u, 1u, 3u);
        meshPool = std::make_unique<ThreadPool>(threads);
    }

    LevelRenderer::~LevelRenderer() {
        Stop();
    }

    void LevelRenderer::Stop() {
        stopping = true;
        if (meshPool)
            meshPool->Stop();
    }

    void LevelRenderer::ConfigureLighting(const std::string& packDir) {
        LightRegistry& registry = LightRegistry::Instance();
        const std::string path = packDir + "/light.json";
        std::string rules = ReadText(path);
        if (rules.empty()) {
            rules = ReadText(paths::ExeDir() + "/resources/light.json");
            if (!rules.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(packDir, ec);
                std::ofstream(path, std::ios::binary) << rules;
            }
        }
        if (!rules.empty())
            registry.LoadRules(rules);
        registry.LoadBehaviorPack(options.bedrockSamplesDir + "/repo/behavior_pack/blocks");
        registry.SetDerivedOpacity([this](uint16_t id) { return BlockLightOpacity(blocks, id); });
        registry.SetOcclusion([](uint16_t id, FaceOcclusion& out) { return BlockFaceOcclusion(id, out); });
        lightingConfigured = true;
    }

    void LevelRenderer::LoadSkyTextures() {
        if (skyTexturesLoaded)
            return;
        const std::string root = options.bedrockSamplesDir + "/repo/resource_pack/textures/environment/";
        Texture sun, moon, clouds;
        auto load = [&](const char* file, Texture& out) {
            if (!texturePackRoot.empty() &&
                LoadTexture(texturePackRoot + "/textures/environment/" + file, out) && out.width > 0)
                return true;
            return LoadTexture(root + file, out);
        };
        if (!crosshairLoaded) {
            Texture icons;
            const std::string guiRoot = options.bedrockSamplesDir + "/repo/resource_pack/textures/gui/";
            if ((!texturePackRoot.empty() && LoadTexture(texturePackRoot + "/textures/gui/icons.png", icons) &&
                 icons.width >= 16) ||
                LoadTexture(guiRoot + "icons.png", icons)) {
                context.UploadCrosshair(icons);
                crosshairLoaded = true;
            }
        }
        if (!load("sun.png", sun) || !load("moon_phases.png", moon) || !load("clouds.png", clouds))
            return;

        unsigned char lowest = 255, highest = 0;
        for (size_t i = 3; i < clouds.pixels.size(); i += 4) {
            lowest = std::min(lowest, clouds.pixels[i]);
            highest = std::max(highest, clouds.pixels[i]);
        }
        const bool byAlpha = lowest != highest;
        for (size_t i = 0; i < clouds.pixels.size(); i += 4) {
            const unsigned char mask = byAlpha ? clouds.pixels[i + 3] : clouds.pixels[i];
            clouds.pixels[i] = clouds.pixels[i + 1] = clouds.pixels[i + 2] = 255;
            clouds.pixels[i + 3] = mask >= 10 ? 255 : 0;
        }

        context.UploadSkyTextures(sun, moon, clouds);
        skyTexturesLoaded = true;
    }

    void LevelRenderer::InvalidateMeshes() {
        LightRegistry::Instance().Invalidate();
        meshVersions.clear();
        inFlight.clear();
        quiet.clear();
        generation++;
        {
            std::lock_guard lock(doneMutex);
            done.clear();
        }
        context.ClearChunkMeshes();
        rescan = true;
    }

    void LevelRenderer::ReloadAssetsIfNeeded() {
        const bool complete = atlasUploaded && blocks.Complete() && biomeTints.Ready();
        if (complete)
            return;

        const auto now = std::chrono::steady_clock::now();
        if (now - lastAssetAttempt < kAssetRetry)
            return;
        lastAssetAttempt = now;

        const std::string packDir = options.resourcePackDir + "/Perlin";
        const std::string vanillaDir = options.bedrockSamplesDir + "/repo/resource_pack";
        if (!BedrockSamples::Available(options.bedrockSamplesDir))
            return;
        if (!texturePackResolved) {
            texturePackResolved = true;
            std::string error;
            const std::string root =
                ResolveTexturePack(paths::DataDir() + "/texturepacks", options.texturePack, error);
            if (!error.empty())
                std::cout << "[textures] " << error << " (using default textures)" << std::endl;
            blocks.SetTexturePack(root);
            texturePackRoot = root;
        }
        LoadSkyTextures();
        const bool blocksOk = blocks.EnsureLoaded(packDir, vanillaDir);
        const bool tintsOk = blocksOk && biomeTints.EnsureLoaded(vanillaDir);
        if (!blocksOk)
            return;
        if (!lightingConfigured)
            ConfigureLighting(packDir);

        const bool changed =
            !atlasUploaded || blocks.TilesLoaded() > uploadedTiles || (tintsOk && !tintsWereReady);
        if (!changed)
            return;

        tintsWereReady = tintsOk;
        uploadedTiles = blocks.TilesLoaded();
        context.UploadAtlas(blocks.AtlasPixels().data(), BlockLibrary::TilePx(), blocks.TilesLoaded());
        atlasUploaded = true;
        InvalidateMeshes();
    }

    void LevelRenderer::UploadFinishedMeshes() {
        std::vector<MeshResult> finished;
        {
            std::lock_guard lock(doneMutex);
            finished.swap(done);
        }
        const uint32_t current = generation.load();
        // GPU uploads are budgeted per frame so a burst of finished chunks cannot stall a frame.
        const auto uploadStart = std::chrono::steady_clock::now();
        size_t consumed = 0;
        for (MeshResult& r : finished) {
            if (consumed > 0 && std::chrono::steady_clock::now() - uploadStart > std::chrono::milliseconds(3))
                break;
            consumed++;
            inFlight.erase(r.pos);
            if (!meshVersions.count(r.pos))
                continue; // the chunk was unloaded while it was being meshed
            if (r.generation == current)
                context.UpdateChunkMesh(r.pos.x, r.pos.z, *r.mesh, r.version);
        }
        if (consumed < finished.size()) {
            std::lock_guard lock(doneMutex);
            done.insert(done.begin(), std::make_move_iterator(finished.begin() + consumed),
                        std::make_move_iterator(finished.end()));
        }
    }

    void LevelRenderer::SnapshotVersions(Level& level) {
        snapshot.clear();
        if (const uint64_t cleared = level.clearCounter.load(); cleared != lastClear) {
            // The whole world was replaced (dimension change): drop every mesh built from the old one.
            lastClear = cleared;
            for (const auto& [pos, version] : meshVersions) {
                (void)version;
                context.RemoveChunkMesh(pos.x, pos.z);
            }
            meshVersions.clear();
            quiet.clear();
        }
        std::unordered_map<ChunkPos, uint64_t, ChunkPosHash> versions;
        {
            std::lock_guard lock(level.mutex);
            versions.reserve(level.chunks.size());
            for (const auto& [pos, chunk] : level.chunks)
                versions.emplace(pos, chunk.version + 1);
        }
        // Chunks the server stopped streaming: drop their meshes.
        for (auto it = meshVersions.begin(); it != meshVersions.end();) {
            if (versions.count(it->first)) {
                ++it;
                continue;
            }
            context.RemoveChunkMesh(it->first.x, it->first.z);
            quiet.erase(it->first);
            it = meshVersions.erase(it);
        }
        snapshot.reserve(versions.size());
        for (const auto& [pos, version] : versions) {
            uint64_t key = 1469598103934665603ull;
            for (int dz = -1; dz <= 1; dz++)
                for (int dx = -1; dx <= 1; dx++) {
                    const auto neighbor = versions.find(ChunkPos{pos.x + dx, pos.z + dz});
                    key = Combine(key, neighbor == versions.end() ? 0 : neighbor->second);
                }
            snapshot.emplace_back(pos, key);
        }
    }

    void LevelRenderer::CollectPending(float camX, float camZ, const Frustum& frustum) {
        pending.clear();
        for (const auto& [pos, version] : snapshot) {
            const auto meshed = meshVersions.find(pos);
            if (meshed != meshVersions.end() && meshed->second == version) {
                quiet.erase(pos);
                continue;
            }

            Quiet& q = quiet[pos];
            if (q.version != version) {
                q = {version, 0};
                continue;
            }
            q.frames++;
            if (meshed != meshVersions.end() && q.frames < kRemeshQuietFrames)
                continue;
            if (q.frames < kMinQuietFrames)
                continue;

            const float dx = pos.x * 16.0f + 8.0f - camX;
            const float dz = pos.z * 16.0f + 8.0f - camZ;
            float distance = dx * dx + dz * dz;
            const float x0 = pos.x * 16.0f, z0 = pos.z * 16.0f;
            if (!frustum.Visible(x0, static_cast<float>(Chunk::MIN_Y), z0, x0 + 16.0f,
                                 static_cast<float>(Chunk::MIN_Y + Chunk::HEIGHT), z0 + 16.0f))
                distance += kOffscreenPenalty;
            pending.push_back({distance, pos, version});
        }
    }

    void LevelRenderer::ScheduleAsync(Level& level, size_t freeSlots) {
        const size_t take = std::min(freeSlots, pending.size());
        if (!take)
            return;
        std::partial_sort(pending.begin(), pending.begin() + take, pending.end(),
                          [](const Pending& a, const Pending& b) { return a.distance < b.distance; });

        for (size_t i = 0; i < take; i++) {
            const Pending job = pending[i];
            if (inFlight.contains(job.pos))
                continue;
            meshVersions[job.pos] = job.version;
            quiet.erase(job.pos);
            inFlight.insert(job.pos);

            const uint32_t gen = generation.load();
            Level* target = &level;
            meshPool->Submit([this, target, job, gen] {
                if (stopping)
                    return;
                auto mesh = std::make_unique<Tessellator>();
                const auto t0 = std::chrono::steady_clock::now();
                mesh->Begin();
                BuildChunkMesh(*target, job.pos, *mesh, &blocks, &biomeTints);
                mesh->End();
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - t0).count();
                if (ms > 40)
                    std::printf("[perf] slow chunk mesh (%d,%d): %lld ms, %zu vertices\n", job.pos.x, job.pos.z,
                                static_cast<long long>(ms), mesh->Vertices().size());
                std::lock_guard lock(doneMutex);
                done.push_back({job.pos, job.version, gen, std::move(mesh)});
            });
        }
    }

    void LevelRenderer::MeshInline(Level& level) {
        std::sort(pending.begin(), pending.end(),
                  [](const Pending& a, const Pending& b) { return a.distance < b.distance; });
        const auto start = std::chrono::steady_clock::now();
        for (const Pending& job : pending) {
            if (std::chrono::steady_clock::now() - start > kInlineBudget)
                break;
            meshVersions[job.pos] = job.version;
            quiet.erase(job.pos);
            Tessellator mesh;
            mesh.Begin();
            BuildChunkMesh(level, job.pos, mesh, &blocks, &biomeTints);
            mesh.End();
            context.UpdateChunkMesh(job.pos.x, job.pos.z, mesh, job.version);
        }
    }

    void LevelRenderer::Update(Level* level, float camX, float camZ, const Frustum& frustum) {
        if (!level)
            return;

        UploadFinishedMeshes();

        const uint64_t change = level->changeCounter.load();
        const bool scan = level != lastLevel || change != lastChange || rescan;
        if (scan) {
            lastLevel = level;
            lastChange = change;
            SnapshotVersions(*level);
        }

        CollectPending(camX, camZ, frustum);

        const bool async = atlasUploaded && blocks.Complete() && biomeTints.Ready();
        if (async)
            ScheduleAsync(*level, kMaxInFlight > inFlight.size() ? kMaxInFlight - inFlight.size() : 0);
        else
            MeshInline(*level);

        rescan = scan && (!pending.empty() || !quiet.empty() || !inFlight.empty());
    }

    void LevelRenderer::Render(const Frustum& frustum, float camX, float camZ) {
        context.DrawChunkMeshes(frustum, camX, camZ);
    }

    void LevelRenderer::TickAnimation(uint64_t step) {
        if (!atlasUploaded)
            return;
        blocks.PollAnimation(step, [&](int tile, const uint8_t* rgba) {
            context.UpdateAtlasRegion(tile, rgba);
        });
    }
}
