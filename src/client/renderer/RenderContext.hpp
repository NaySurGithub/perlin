#pragma once
#include "client/renderer/RenderStates.hpp"
#include "client/renderer/Frustum.hpp"
#include "client/renderer/Lightmap.hpp"
#include "client/renderer/SkyGeometry.hpp"
#include "client/resources/TextureLoader.hpp"

#include <cstdint>
#include <string>
#include <vector>

class AppPlatform;

namespace perlin {
    class Tessellator;
    struct Options;

    struct SkyFrame {
        bool enabled = false;
        float skyColor[3] = {};
        float sunAngle = 0.0f;
        float moonAngle = 0.0f;
        float starAngle = 0.0f;
        float rainBrightness = 1.0f;
        float starBrightness = 0.0f;
        float sunrise[4] = {};
        int moonPhase = 0;
        bool darkDisc = false;
    };

    struct CloudDraw {
        bool enabled = false;
        bool fancy = true;
        int rangeChunks = 128;
        float color[4] = {};
        float height = 192.33f;
        double gameTicks = 0.0;
    };

    struct UiVertex {
        float x, y, z;
        uint32_t abgr;
        float u, v;
    };
    static_assert(sizeof(UiVertex) == 24);

    enum class UiLayer : uint8_t {
        World,
        WorldLate,
        Hand,
        Gui,
        Doll,
        Top
    };

    struct UiDraw {
        uint32_t mesh = 0;
        uint32_t texture = 0;
        float model[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        bool scissor = false;
        int scissorRect[4] = {0, 0, 0, 0};
        bool premultiplied = true;
        float alphaCutoff = 0.0f;
        bool depth = false;
        bool depthWrite = true;
        uint8_t blend = 0;
        bool customBlend = false;
        BlendFactor blendSrc = BlendFactor::SrcAlpha;
        BlendFactor blendDst = BlendFactor::InvSrcAlpha;
        DepthFunc depthFunc = DepthFunc::LessEqual;
        CullMode cull = CullMode::None;
        bool colorWrite = true;
        bool alphaWrite = true;
        bool alphaToCoverage = false;
        float overlay[4] = {0, 0, 0, 0};
        uint8_t emissive = 0;
        uint32_t glintTexture = 0;
        float glintMatrix[4] = {1, 0, 0, 1};
        float glintOffset[2] = {0, 0};
        float glintStrength = 0.9f;
        // Only cast a shader-pack shadow, don't draw (your own player in first person).
        bool shadowOnly = false;
    };

    struct FrameParams {
        float viewProjection[16];
        float skyViewProjection[16];
        float cameraPosition[3];
        float fogColor[4];
        float environmentalFogStart;
        float environmentalFogEnd;
        float renderDistanceFogStart;
        float renderDistanceFogEnd;
        float skyFogEnd;
        float cloudFogEnd;
        bool rgss;
        SkyFrame sky;
        CloudDraw cloud;
        float timeSeconds;
        float motionBlur = 0.0f;
        float bloom = 0.0f;              // 0..1
        int shadowQuality = 2;           // 0 off .. 4 ultra (shader packs with shadows)
        int shadowDistance = 0;          // blocks, 0 = the pack's default
        bool shadowEntities = true;      // entities cast pack shadows
        int cameraMedium = 0;            // what the camera eye is in: 0 air, 1 water, 2 lava
        float waterSurfaceAbove = 0.0f;  // underwater: height of the water surface above the eye, in blocks
        float menuBlur = 0.0f;
        float renderScale = 1.0f;       // the world is drawn at this fraction of the window size, then rebuilt (upscaler)
        int upscaler = 0;               // 0 bilinear, 1 bicubic, 2 FSR 1 style edge-adaptive, 3 temporal
        float view[16];                 // the camera's view and projection (OpenGL clip convention), without any jitter
        float projection[16];
        int frameGen = 1;               // total frame multiplier: 1 off, 2..4 = generated frames between real ones
        bool teleported = false;        // the camera jumped (respawn, dimension change): temporal history and generation restart
        float upscaleSharpness = 0.0f;  // 0..1, contrast-adaptive sharpen after the upscale
        bool crosshair = false;
        int guiScale = 0;
        float screenTint[4];
        uint32_t frameIndex;
        float handProjection[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        int dynMode = 0;
        int dynCount = 0;
        float dynLights[8][4] = {};
        float packSettings[32] = {};
        float heldLight = 0.0f;
        float waterTile = -1.0f;
        float lavaTile = -1.0f;
        float waterFlowTile = -1.0f;
        uint8_t lightmap[kLightmapBytes];
    };

    class RenderContext {
    public:
        virtual ~RenderContext() = default;

        virtual bool Initialize(AppPlatform& platform, const Options& options) = 0;
        virtual void Shutdown() = 0;
        virtual bool ReloadShaders() { return false; }
        // True while a shader pack's sun shadow map is in use.
        virtual bool PackShadowsActive() const { return false; }
        virtual void SetVsync(bool) {}
        virtual std::string RendererName() const { return {}; }
        virtual std::string GpuVendor() const { return {}; }
        virtual std::string GpuDescription() const { return {}; }

        virtual void BeginFrame(const FrameParams& params) = 0;
        virtual void EndFrame() = 0;
        // Frame generation: after EndFrame, present generated frame `index` of `total` (1..total-1). False when none can be made.
        virtual bool PresentGenerated(int /*index*/, int /*total*/) { return false; }
        virtual void RequestScreenshot(const std::string& path) = 0;

        virtual void UploadCrosshair(const Texture& icons) = 0;
        virtual void DrawHudCursor(float x, float y, float w, float h) = 0;
        virtual void UploadSkyTextures(const Texture& sun, const Texture& moon, const Texture& clouds) = 0;
        virtual void UploadAtlas(const uint8_t* rgba, int tilePx, int tiles) = 0;
        // Per-tile foliage kinds for shader packs (see BlockLibrary::FoliageFlags).
        virtual void UploadTileFlags(const std::vector<uint8_t>&) {}
        virtual int MaxAtlasLayers(int) const { return 2048; }
        virtual void UpdateAtlasRegion(int tile, const uint8_t* rgba) = 0;

        virtual uint32_t UiCreateTexture(int width, int height, const uint8_t* rgba, bool linear, bool repeat = false) = 0;
        virtual void UiDestroyTexture(uint32_t texture) = 0;
        virtual uint32_t UiCreateMesh(const UiVertex* vertices, uint32_t vertexCount, const uint32_t* indices,
                                      uint32_t indexCount) = 0;
        virtual uint32_t UiCreateFrameMesh(const UiVertex* vertices, uint32_t vertexCount, const uint32_t* indices,
                                           uint32_t indexCount) {
            return UiCreateMesh(vertices, vertexCount, indices, indexCount);
        }
        virtual void UiDestroyMesh(uint32_t mesh) = 0;
        // Meshes currently holding their own GPU buffers (the UI backend watches this for leaks).
        virtual uint32_t UiLiveMeshes() const { return 0; }
        virtual void UiSubmit(UiLayer layer, const UiDraw& draw) = 0;
        virtual void FramebufferSize(int& width, int& height) const = 0;

        virtual void UpdateChunkMesh(int cx, int cz, const Tessellator& tess, uint64_t version) = 0;
        virtual void RemoveChunkMesh(int cx, int cz) = 0;
        virtual void UpdateLodTile(int tx, int tz, int size, const Tessellator& tess, uint64_t version) = 0;
        virtual void RemoveLodTile(int tx, int tz) = 0;
        virtual void ClearChunkMeshes() = 0;
        virtual void DrawChunkMeshes(const Frustum& frustum, float camX, float camZ) = 0;
    };
}
