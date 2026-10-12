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
        bool crosshair = false; // draw the inverting crosshair at the screen centre
        float screenTint[4]; // full-screen colour overlay (damage flash); alpha 0 draws nothing
        uint32_t frameIndex;
        float heldLight = 0.0f;      // block light level of the item in hand (0 = none), for dynamic lighting
        float waterTile = -1.0f;     // atlas layer of the water texture, so shader packs can recognise water
        float lavaTile = -1.0f;
        float waterFlowTile = -1.0f;
        uint8_t lightmap[kLightmapBytes];
    };

    class RenderContext {
    public:
        virtual ~RenderContext() = default;

        virtual bool Initialize(AppPlatform& platform, const Options& options) = 0;
        virtual void Shutdown() = 0;
        // Graphics backend in use (for example "Direct3D 11"); empty before Initialize.
        virtual std::string RendererName() const { return {}; }

        virtual void BeginFrame(const FrameParams& params) = 0;
        virtual void EndFrame() = 0;
        virtual void RequestScreenshot(const std::string& path) = 0;

        // `icons` is the GUI sheet (textures/gui/icons.png); the crosshair is its top-left 15/256 square.
        virtual void UploadCrosshair(const Texture& icons) = 0;
        virtual void UploadSkyTextures(const Texture& sun, const Texture& moon, const Texture& clouds) = 0;
        virtual void UploadAtlas(const uint8_t* rgba, int tilePx, int tiles) = 0;
        virtual void UpdateAtlasRegion(int tile, const uint8_t* rgba) = 0;

        virtual void UpdateChunkMesh(int cx, int cz, const Tessellator& tess, uint64_t version) = 0;
        virtual void RemoveChunkMesh(int cx, int cz) = 0;
        virtual void ClearChunkMeshes() = 0;
        virtual void DrawChunkMeshes(const Frustum& frustum, float camX, float camZ) = 0;
    };
}
