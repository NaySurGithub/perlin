#pragma once
#include "client/renderer/RangeAllocator.hpp"
#include "client/renderer/RenderContext.hpp"
#include "client/renderer/Tessellator.hpp"
#include "client/renderer/shader/ShaderManager.hpp"

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
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
        bool PresentGenerated(int index, int total) override;
        void RequestScreenshot(const std::string& path) override;
        void UploadSkyTextures(const Texture& sun, const Texture& moon, const Texture& clouds) override;
        void UploadCrosshair(const Texture& icons) override;
        void DrawHudCursor(float x, float y, float w, float h) override;
        void UploadAtlas(const uint8_t* rgba, int tilePx, int tiles) override;
        int MaxAtlasLayers(int tilePx) const override;
        bool ReloadShaders() override;
        bool PackShadowsActive() const override { return shadow.enabled; }
        void UploadTileFlags(const std::vector<uint8_t>& flags) override;
        void SetVsync(bool enabled) override;
        void UpdateAtlasRegion(int tile, const uint8_t* rgba) override;
        uint32_t UiCreateTexture(int width, int height, const uint8_t* rgba, bool linear, bool repeat = false) override;
        void UiDestroyTexture(uint32_t texture) override;
        uint32_t UiCreateMesh(const UiVertex* vertices, uint32_t vertexCount, const uint32_t* indices,
                              uint32_t indexCount) override;
        uint32_t UiCreateFrameMesh(const UiVertex* vertices, uint32_t vertexCount, const uint32_t* indices,
                                   uint32_t indexCount) override;
        void UiDestroyMesh(uint32_t mesh) override;
        uint32_t UiLiveMeshes() const override;
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

        // Sun shadow map. The terrain part is cached in two maps: the world samples the front one
        // while the back one is rebuilt a slice of chunks per frame (a rebuild never costs a whole
        // map in a single frame), then they swap. Mobs and players move every frame, so when they
        // cast shadows the front map is copied into a third map each frame and they are drawn on top.
        struct ShadowMap {
            bgfx::FrameBufferHandle fb = BGFX_INVALID_HANDLE;
            bgfx::TextureHandle color = BGFX_INVALID_HANDLE;
            // Coloured light through stained glass etc.: half size, rgb = tint, a = nearest glass depth.
            bgfx::FrameBufferHandle tintFb = BGFX_INVALID_HANDLE;
            bgfx::TextureHandle tint = BGFX_INVALID_HANDLE;
            float sample[16] = {};
            float raster[16] = {};
            float info[4] = {};
        };
        struct ShadowState {
            ShadowMap maps[2];
            int front = -1;                                       // map with a finished build, -1 = none yet
            bgfx::TextureHandle buildDepth = BGFX_INVALID_HANDLE; // shared by both terrain maps
            bgfx::FrameBufferHandle entityFb = BGFX_INVALID_HANDLE;
            bgfx::TextureHandle entityColor = BGFX_INVALID_HANDLE;
            bgfx::TextureHandle entityDepth = BGFX_INVALID_HANDLE;
            bool entityFailed = false;
            int size = 0;
            float distance = 80.0f;
            bool enabled = false;
            bool active = false;       // the world samples a shadow map this frame
            float sample[16] = {};     // what the world samples this frame
            float raster[16] = {};
            float info[4] = {};
            bgfx::TextureHandle sampled = BGFX_INVALID_HANDLE;
            bgfx::TextureHandle sampledTint = BGFX_INVALID_HANDLE;
            bool tintEnabled = false;
            int64_t key[6] = {};       // key of the newest build (finished or in progress)
            bool haveKey = false;
            int framesSince = 0;
            bool entities = false;     // entity meshes are drawn into the entity map this frame
            float sampleE[16] = {};    // the entity map: exact light direction, redrawn every frame
            float rasterE[16] = {};
            float infoE[4] = {};
            // A finished rebuild fades in over the map in use instead of replacing it in one step.
            bool fading = false;
            float fadeT = 0.0f;        // 0..1 progress
            float fadeWeight = 0.0f;   // eased weight of the next map this frame (0 = not fading)
            double lastTime = 0.0;
            float sampleB[16] = {};
            float infoB[4] = {};
            bgfx::TextureHandle sampledB = BGFX_INVALID_HANDLE;
            bgfx::TextureHandle sampledTintB = BGFX_INVALID_HANDLE;
            // Build in progress (into maps[1 - front]).
            bool building = false;
            int buildSlice = 0, buildSlices = 1;
            std::vector<std::pair<int64_t, uint8_t>> buildList; // chunk key, pass, nearest first
            size_t buildCursor = 0;
            glm::vec3 buildRight{}, buildUp{}, buildFwd{};
            float buildCx = 0, buildCy = 0, buildCz = 0;
        } shadow;
        // Shadow settings the map was built with; a change (or a shader reload) rebuilds it.
        int shadowQualityBuilt = -1, shadowDistanceBuilt = -1;
        bool shadowConfigDirty = true;
        void InitShadows(int quality, int distance);
        void DestroyShadows();
        void RenderShadows(float camX, float camY, float camZ);
        bool EnsureEntityShadowMap();

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
        // A cube around the camera: a sky covering every direction (shader packs with "skyBox").
        bgfx::VertexBufferHandle skyBoxBuffer = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle skyBoxIndices = BGFX_INVALID_HANDLE;
        bgfx::VertexBufferHandle screenBuffer = BGFX_INVALID_HANDLE;
        void DrawScreenTint();

        struct MotionBlur {
            bgfx::TextureHandle worldColor = BGFX_INVALID_HANDLE, worldDepth = BGFX_INVALID_HANDLE;
            bgfx::FrameBufferHandle worldFb = BGFX_INVALID_HANDLE;
            bgfx::TextureHandle histColor[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
            bgfx::FrameBufferHandle histFb[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
            bgfx::UniformHandle sampCur = BGFX_INVALID_HANDLE, sampHist = BGFX_INVALID_HANDLE, params = BGFX_INVALID_HANDLE;
            bgfx::UniformHandle fx = BGFX_INVALID_HANDLE; // u_postFx: x underwater wobble strength, y time
            int read = 0;
            bool valid = false;
            bool targets = false;
            bool scaled = false;  // the offscreen targets are smaller than the window (render scale)
            int w = 0, h = 0;     // their size when scaled
            float lastTime = 0.0f;
        } blur;
        // What this frame needs offscreen, decided at the start of BeginFrame.
        struct Plan {
            int w = 0, h = 0;          // size the world is drawn at
            bool scaled = false;       // smaller than the window
            bool temporal = false;     // temporal upscaler (jittered projection, history)
            bool frameGen = false;     // route the whole frame through a texture so generated frames can reuse it
            bool wantDepth = false;    // the world depth must be readable
            float jitter[2] = {0, 0};  // projection jitter in clip space (x, y)
            float jitterUv[2] = {0, 0}; // the same in texture coordinates of this backend
        } plan;
        void PlanFrame(const FrameParams& params);
        // Render scale: reconstruction of the full-size image from the smaller world image.
        struct Upscale {
            bgfx::TextureHandle tex[3] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
            bgfx::FrameBufferHandle fb[3] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
            bgfx::UniformHandle mode = BGFX_INVALID_HANDLE, size = BGFX_INVALID_HANDLE;
            bgfx::UniformHandle taa = BGFX_INVALID_HANDLE, taaSize = BGFX_INVALID_HANDLE, taaJit = BGFX_INVALID_HANDLE,
                                taaInvVp = BGFX_INVALID_HANDLE, taaPrevVp = BGFX_INVALID_HANDLE;
            bgfx::UniformHandle sampDepth = BGFX_INVALID_HANDLE, sampHist = BGFX_INVALID_HANDLE;
            int w = 0, h = 0;
            bool failed = false;
            // Temporal state: tex[history] holds the last output.
            int history = 0;
            bool historyValid = false;
            uint32_t frameIndex = 0;
        } upscale;
        void EnsureUpscaleUniforms();
        bool EnsureUpscale();
        void DestroyUpscale();
        bgfx::TextureHandle RunUpscale(bgfx::TextureHandle source, int sourceW, int sourceH, const FrameParams& params);
        // Camera history for reprojection (temporal upscaler, frame generation). Matrices are kept in double and the eye is
        // subtracted before use, so far from the origin they stay precise.
        struct CameraHistory {
            glm::dmat4 view{1.0}, proj{1.0}, prevView{1.0}, prevProj{1.0};
            glm::dvec3 eye{0.0}, prevEye{0.0};
            bool haveCur = false, havePrev = false;
            bool teleported = false;
            int sizeW = 0, sizeH = 0;
            double time = 0.0, prevTime = 0.0; // seconds when each pose was captured
        } cam;
        // Frame generation.
        struct FrameGen {
            bgfx::TextureHandle finalColor = BGFX_INVALID_HANDLE, finalDepth = BGFX_INVALID_HANDLE, worldCopy = BGFX_INVALID_HANDLE;
            bgfx::FrameBufferHandle finalFb = BGFX_INVALID_HANDLE;
            bgfx::UniformHandle gen = BGFX_INVALID_HANDLE, invVp = BGFX_INVALID_HANDLE, curVp = BGFX_INVALID_HANDLE;
            bgfx::UniformHandle sampWorld = BGFX_INVALID_HANDLE, sampDepth = BGFX_INVALID_HANDLE, sampFinal = BGFX_INVALID_HANDLE;
            bool ready = false, failed = false;
            bool valid = false; // a real frame has been drawn through the pipeline
            int w = 0, h = 0;
            bool haveDepth = false; // the depth of the real frame can be sampled
        } fg;
        bool EnsureFrameGen();
        void DestroyFrameGen();
        void SubmitFrameGenCopies(float origin);
        // Shader packs that set "sceneCopy" get the opaque scene (colour + depth) copied after
        // entities and before the translucent pass, for screen-space reflections/refraction.
        struct SceneCopy {
            bgfx::TextureHandle color = BGFX_INVALID_HANDLE, depth = BGFX_INVALID_HANDLE;
            bool valid = false;
            bool failed = false;
        } scene;
        bool depthReadable = false;
        bool EnsureSceneCopy();
        void DestroySceneCopy();
        // Bloom: a 4-level downsample/upsample chain over the offscreen world.
        struct Bloom {
            bgfx::TextureHandle level[4] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
            bgfx::FrameBufferHandle fb[4] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
            bgfx::UniformHandle params = BGFX_INVALID_HANDLE, texel = BGFX_INVALID_HANDLE, composite = BGFX_INVALID_HANDLE,
                                sampler = BGFX_INVALID_HANDLE;
            bool ready = false, failed = false;
        } bloom;
        bool EnsureBloom();
        void DestroyBloom();
        void RunBloom(float strength);
        bgfx::TextureHandle cloudMapTexture = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle tileFlagsTexture = BGFX_INVALID_HANDLE; // 256 x 64, r = foliage kind
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

        std::unordered_map<const uint8_t*, std::vector<std::vector<uint8_t>>> animatedTiles;
        int atlasLayers = 0;
        int tileSize = 16;
        bgfx::TextureHandle crosshairTexture = BGFX_INVALID_HANDLE;
        int crosshairPx = 0;

        std::vector<VisibleChunk> visible;
        uint32_t drawCalls = 0;

        std::vector<Page> pages;
        std::unordered_map<int64_t, GpuChunk> gpuChunks;
        std::unordered_map<int64_t, GpuChunk> lodChunks;

        // UI meshes are handed out as ids = (generation << kUiMeshSlotBits) | (slot + 1). A slot's
        // generation changes every time it is freed, so a stale id (destroyed twice, kept past its
        // frame, or freed by the renderer) never reaches the mesh that reuses the slot.
        static constexpr uint32_t kUiMeshSlotBits = 20;
        static constexpr uint32_t kUiMeshSlotMask = (1u << kUiMeshSlotBits) - 1u;
        struct UiMeshGpu {
            bgfx::VertexBufferHandle vb = BGFX_INVALID_HANDLE;
            bgfx::IndexBufferHandle ib = BGFX_INVALID_HANDLE;
            uint32_t indexCount = 0;
            uint32_t generation = 0;
            bool used = false;
            bool transient = false;
            bgfx::TransientVertexBuffer tvb{};
            bgfx::TransientIndexBuffer tib{};
        };
        UiMeshGpu* LookupUiMesh(uint32_t id);
        uint32_t StoreUiMesh(const UiMeshGpu& mesh);
        void ReleaseUiMeshSlot(uint32_t slot);
        void ReleaseFrameMeshes();
        std::vector<uint32_t> freeUiMeshes;
        std::vector<uint32_t> frameUiMeshes;   // made by UiCreateFrameMesh this frame; freed after bgfx::frame()
        uint32_t staticUiMeshes = 0;           // live meshes holding bgfx vertex/index buffers
        uint32_t staticUiMeshWarnAt = 2048;
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
