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
        std::string GpuVendor() const override;
        std::string GpuDescription() const override;
        void Shutdown() override;
        void BeginFrame(const FrameParams& params) override;
        void EndFrame() override;
        void RequestScreenshot(const std::string& path) override;
        void UploadSkyTextures(const Texture& sun, const Texture& moon, const Texture& clouds) override;
        void UploadCrosshair(const Texture& icons) override;
        void DrawHudCursor(float x, float y, float w, float h) override;
        void UploadAtlas(const uint8_t* rgba, int tilePx, int tiles) override;
        void UpdateAtlasRegion(int tile, const uint8_t* rgba) override;
        uint32_t UiCreateTexture(int width, int height, const uint8_t* rgba, bool linear, bool repeat = false) override;
        void UiDestroyTexture(uint32_t texture) override;
        uint32_t UiCreateMesh(const UiVertex* vertices, uint32_t vertexCount, const uint32_t* indices,
                              uint32_t indexCount) override;
        void UiDestroyMesh(uint32_t mesh) override;
        void UiSubmit(UiLayer layer, const UiDraw& draw) override;
        void FramebufferSize(int& w, int& h) const override { w = width; h = height; }
        void UpdateChunkMesh(int cx, int cz, const Tessellator& tess, uint64_t version) override;
        void RemoveChunkMesh(int cx, int cz) override;
        void UpdateLodTile(int tx, int tz, int size, const Tessellator& tess, uint64_t version) override;
        void RemoveLodTile(int tx, int tz) override;
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

            std::array<bgfx::DynamicIndexBufferHandle, kRenderLayerCount> ib;
            RangeAllocator vAlloc;
            std::array<RangeAllocator, kRenderLayerCount> iAlloc;
        };
        struct GpuSection {
            float minY, maxY;
            int sy = 0;
            std::array<uint32_t, kRenderLayerCount> start;
            std::array<uint32_t, kRenderLayerCount> count;
        };
        struct GpuChunk {
            int page = -1;
            uint32_t vOff = 0, vCount = 0;
            std::array<uint32_t, kRenderLayerCount> iOff{};
            std::array<uint32_t, kRenderLayerCount> layerCount{};
            std::vector<GpuSection> sections;
            uint64_t version = 0;
            float minY = 0, maxY = 0;
            int cx = 0, cz = 0;
            int size = 16;
            int visMin = 0;
            std::vector<uint16_t> conn;
            uint32_t secVis = ~0u;
        };
        void UpdateCaveVisibility(float camX, float camY, float camZ);
        uint64_t meshGeneration = 1;
        uint64_t caveGeneration = 0;
        int caveCam[3] = {0, 0, 0};
        bool caveCulling = true;

        bool CreatePage(uint32_t vCap, const std::array<uint32_t, kRenderLayerCount>& iCap);
        void UploadMesh(std::unordered_map<int64_t, GpuChunk>& target, int cx, int cz, int size, const Tessellator& tess,
                        uint64_t version);
        bool AllocMesh(uint32_t v, const std::array<uint32_t, kRenderLayerCount>& i, GpuChunk& out);
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

        struct ShadowState {
            bgfx::FrameBufferHandle fb = BGFX_INVALID_HANDLE;
            bgfx::TextureHandle color = BGFX_INVALID_HANDLE;
            int size = 0;
            float distance = 80.0f;
            bool enabled = false;
            bool active = false;
            float sample[16] = {};
            float raster[16] = {};
            float info[4] = {};
            int64_t key[6] = {};
            uint64_t generation = 0;
            int framesSince = 0;
        } shadow;
        void InitShadows(const Options& options);
        void DestroyShadows();
        void RenderShadows(float camX, float camY, float camZ);

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

        struct MotionBlur {
            bgfx::TextureHandle worldColor = BGFX_INVALID_HANDLE, worldDepth = BGFX_INVALID_HANDLE;
            bgfx::FrameBufferHandle worldFb = BGFX_INVALID_HANDLE;
            bgfx::TextureHandle histColor[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
            bgfx::FrameBufferHandle histFb[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
            bgfx::UniformHandle sampCur = BGFX_INVALID_HANDLE, sampHist = BGFX_INVALID_HANDLE, params = BGFX_INVALID_HANDLE;
            int read = 0;
            bool valid = false;
            bool targets = false;
            float lastTime = 0.0f;
        } blur;
        bool EnsureMotionBlurTargets();
        void DestroyMotionBlur();
        void SetupMotionBlur(const FrameParams& params);
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
        int crosshairPx = 0;

        std::vector<VisibleChunk> visible;
        uint32_t drawCalls = 0;

        std::vector<Page> pages;
        std::unordered_map<int64_t, GpuChunk> gpuChunks;
        std::unordered_map<int64_t, GpuChunk> lodChunks;

        struct UiMeshGpu {
            bgfx::VertexBufferHandle vb = BGFX_INVALID_HANDLE;
            bgfx::IndexBufferHandle ib = BGFX_INVALID_HANDLE;
            uint32_t indexCount = 0;
            bool used = false;
        };
        struct UiTextureGpu {
            bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
            bool linear = false;
            bool used = false;
        };
        std::vector<UiMeshGpu> uiMeshes;
        std::vector<UiTextureGpu> uiTextures;
        bgfx::VertexLayout uiLayout;
        bgfx::UniformHandle uiSampler = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle uiParams = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle uiOverlay = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle uiGlintSampler = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle uiGlintA = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle uiGlintB = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle uiWhite = BGFX_INVALID_HANDLE;
        void DestroyUi();

        std::mutex shotMutex;
        std::atomic<bool> shotPending{false};
        std::atomic<bool> shotDone{false};
        bool shotOk = false;
    };
}
