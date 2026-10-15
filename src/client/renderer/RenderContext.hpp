#pragma once
#include "client/renderer/Frustum.hpp"
#include "client/renderer/Lightmap.hpp"
#include "client/renderer/SkyGeometry.hpp"
#include "client/resources/TextureLoader.hpp"

#include <cstdint>
#include <string>

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
        Doll
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
        float overlay[4] = {0, 0, 0, 0};
        uint8_t emissive = 0;
        uint32_t glintTexture = 0;
        float glintMatrix[4] = {1, 0, 0, 1};
        float glintOffset[2] = {0, 0};
        float glintStrength = 0.9f;
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
        float menuBlur = 0.0f;
        bool crosshair = false;
        int guiScale = 0;
        float screenTint[4];
        uint32_t frameIndex;
        float handProjection[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        int dynMode = 0;
        int dynCount = 0;
        float dynLights[8][4] = {};
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
        virtual std::string RendererName() const { return {}; }
        virtual std::string GpuVendor() const { return {}; }
        virtual std::string GpuDescription() const { return {}; }

        virtual void BeginFrame(const FrameParams& params) = 0;
        virtual void EndFrame() = 0;
        virtual void RequestScreenshot(const std::string& path) = 0;

        virtual void UploadCrosshair(const Texture& icons) = 0;
        virtual void DrawHudCursor(float x, float y, float w, float h) = 0;
        virtual void UploadSkyTextures(const Texture& sun, const Texture& moon, const Texture& clouds) = 0;
        virtual void UploadAtlas(const uint8_t* rgba, int tilePx, int tiles) = 0;
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
