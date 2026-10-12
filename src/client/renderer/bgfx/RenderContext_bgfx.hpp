#pragma once
#include "client/renderer/RangeAllocator.hpp"
#include "client/renderer/RenderContext.hpp"
#include "client/renderer/Tessellator.hpp"
#include "client/renderer/shader/ShaderManager.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin {
    class RenderContext_bgfx : public RenderContext {
    public:
        RenderContext_bgfx();
        ~RenderContext_bgfx() override;

        bool Initialize(AppPlatform& platform, const Options& options) override;
        std::string RendererName() const override { return rendererName; }
        void Shutdown() override;
        void BeginFrame(const FrameParams& params) override;
        void EndFrame() override;
        void RequestScreenshot(const std::string& path) override;
        void UploadSkyTextures(const Texture& sun, const Texture& moon, const Texture& clouds) override;
        void UploadCrosshair(const Texture& icons) override;
        void UploadAtlas(const uint8_t* rgba, int tilePx, int tiles) override;
        void UpdateAtlasRegion(int tile, const uint8_t* rgba) override;
        void UpdateChunkMesh(int cx, int cz, const Tessellator& tess, uint64_t version) override;
        void RemoveChunkMesh(int cx, int cz) override;
        void ClearChunkMeshes() override;
        void DrawChunkMeshes(const Frustum& frustum, float camX, float camZ) override;

        void SaveScreenshotData(const std::string& path, uint32_t w, uint32_t h, uint32_t pitch,
                                const uint8_t* bgra, bool yflip);

    private:
        std::string rendererName;
        struct GpuChunk;
        struct VisibleChunk {
            float distance;
            const GpuChunk* chunk;
            bool fullyInside;
        };
        struct Page {
            bgfx::DynamicVertexBufferHandle vb = BGFX_INVALID_HANDLE;
            bgfx::DynamicIndexBufferHandle ib = BGFX_INVALID_HANDLE;
            RangeAllocator vAlloc, iAlloc;
        };
        struct GpuSection {
            float minY, maxY;
            std::array<uint32_t, kRenderLayerCount> start;
            std::array<uint32_t, kRenderLayerCount> count;
        };
        struct GpuChunk {
            int page = -1;
            uint32_t vOff = 0, vCount = 0;
            uint32_t iOff = 0, iCount = 0;
            std::array<uint32_t, kRenderLayerCount> layerBase{};
            std::array<uint32_t, kRenderLayerCount> layerCount{};
            std::vector<GpuSection> sections;
            uint64_t version = 0;
            float minY = 0, maxY = 0;
            int cx = 0, cz = 0;
        };

        bool CreatePage(uint32_t vCap, uint32_t iCap);
        bool AllocMesh(uint32_t v, uint32_t i, GpuChunk& out);
        void FreeChunk(GpuChunk& g);
        void DestroyPages();
        void Resize(int w, int h);
        uint32_t ResetFlags() const;
        uint64_t PassState(RenderLayer layer) const;
        void DrawLayer(RenderLayer layer, ShaderProgram program, const Frustum& frustum,
                       const std::vector<VisibleChunk>& chunks, bgfx::TextureHandle atlas);
        void DrawSky();
        void DrawClouds();
        void DestroySkyResources();
        void ConvertDepth(const float in[16], float out[16]) const;
        void UploadLightmap(const uint8_t* pixels);
        static int64_t ChunkKey(int cx, int cz) { return (static_cast<int64_t>(cx) << 32) | (uint32_t)cz; }

        class Callback;
        std::unique_ptr<Callback> callback;

        AppPlatform* platform = nullptr;
        bool vsync = true;
        bool initialized = false;

        bgfx::VertexLayout layout;
        bgfx::VertexLayout positionLayout;
        bgfx::VertexLayout colorLayout;
        bgfx::VertexLayout textureLayout;
        bgfx::VertexBufferHandle topSkyBuffer = BGFX_INVALID_HANDLE;
        bgfx::VertexBufferHandle bottomSkyBuffer = BGFX_INVALID_HANDLE;
        bgfx::VertexBufferHandle screenBuffer = BGFX_INVALID_HANDLE;
        void DrawScreenTint();
        void DrawCrosshair();
        bgfx::VertexBufferHandle starBuffer = BGFX_INVALID_HANDLE;
        bgfx::VertexBufferHandle sunriseBuffer = BGFX_INVALID_HANDLE;
        bgfx::VertexBufferHandle sunBuffer = BGFX_INVALID_HANDLE;
        bgfx::VertexBufferHandle moonBuffer = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle skyFanIndices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle sunriseIndices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle starIndices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle quadIndices = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle sunTexture = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle moonTexture = BGFX_INVALID_HANDLE;
        uint32_t starIndexCount = 0;
        uint32_t skyFanIndexCount = 0;
        uint32_t sunriseIndexCount = 0;
        CloudTexture cloudTexture;
        bgfx::DynamicVertexBufferHandle cloudVertexBuffer = BGFX_INVALID_HANDLE;
        bgfx::DynamicIndexBufferHandle cloudIndexBuffer = BGFX_INVALID_HANDLE;
        uint32_t cloudVertexCapacity = 0;
        uint32_t cloudIndexCapacity = 0;
        uint32_t cloudIndexCount = 0;
        CloudFrame cloudBuiltFrame;
        bool cloudBuiltFancy = false;
        int cloudBuiltRange = 0;
        bool cloudBuilt = false;
        float skyMvp[16] = {};
        FrameParams frame{};
        bgfx::TextureHandle lightmapTex = BGFX_INVALID_HANDLE;
        ShaderManager shaders;
        uint32_t lightmapFlags = 0;
        uint8_t lastLightmap[kLightmapBytes] = {};
        bool lightmapValid = false;
        bgfx::TextureHandle atlasTex = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle whiteTex = BGFX_INVALID_HANDLE;
        float mvp[16] = {};
        uint32_t samplerFlags = 0;
        uint64_t cullState = BGFX_STATE_CULL_CCW;
        bool homogeneousDepth = false;

        int width = 0, height = 0;

        int atlasLayers = 0;
        int tileSize = 16;
        bgfx::TextureHandle crosshairTexture = BGFX_INVALID_HANDLE;
        int crosshairPx = 0; // source size of the crosshair square in texels

        std::vector<VisibleChunk> visible;

        std::vector<Page> pages;
        std::unordered_map<int64_t, GpuChunk> gpuChunks;

        std::mutex shotMutex;
        std::atomic<bool> shotPending{false};
        std::atomic<bool> shotDone{false};
        bool shotOk = false;
    };
}
