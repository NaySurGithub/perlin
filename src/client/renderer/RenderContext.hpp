#pragma once
#include "client/renderer/Frustum.hpp"
#include "client/renderer/Lightmap.hpp"

#include <cstdint>
#include <string>

class AppPlatform;

namespace perlin {
    class Tessellator;
    struct Options;

    struct FrameParams {
        float viewProjection[16];
        float inverseViewProjection[16];
        float cameraPosition[3];
        float renderDistance;
        float skyColor[3];
        float fogColor[3];
        float sunDirection[3];
        float sunriseColor[4];
        float fogStart;
        float fogEnd;
        float celestialAngle;
        float skyDarken;
        float starBrightness;
        float timeSeconds;
        uint32_t frameIndex;
        uint8_t lightmap[kLightmapBytes];
    };

    class RenderContext {
    public:
        virtual ~RenderContext() = default;

        virtual bool Initialize(AppPlatform& platform, const Options& options) = 0;
        virtual void Shutdown() = 0;

        virtual void BeginFrame(const FrameParams& params) = 0;
        virtual void EndFrame() = 0;
        virtual void RequestScreenshot(const std::string& path) = 0;

        virtual void UploadAtlas(const uint8_t* rgba, int size) = 0;
        virtual void UpdateAtlasRegion(int px, int py, const uint8_t* rgba16) = 0;

        virtual void UpdateChunkMesh(int cx, int cz, const Tessellator& tess, uint64_t version) = 0;
        virtual void RemoveChunkMesh(int cx, int cz) = 0;
        virtual void ClearChunkMeshes() = 0;
        virtual void DrawChunkMeshes(const Frustum& frustum, float camX, float camZ) = 0;
    };
}
