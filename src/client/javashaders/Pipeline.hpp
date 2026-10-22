#pragma once
#include "client/javashaders/PipelineConfig.hpp"
#include "client/javashaders/ShaderBackend.hpp"
#include "client/javashaders/Transform.hpp"
#include "client/javashaders/UniformTable.hpp"

#include <bgfx/bgfx.h>

#include <functional>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin::jshader {
    // Where the pack renders (decides the flips needed to keep GL conventions on top-left-origin APIs).
    struct PipelineHost {
        BackendKind backend = BackendKind::OpenGL;
        HlslCompiler hlslCompiler;
        int width = 1280, height = 720;
        bgfx::ViewId firstView = 64;       // the pipeline owns [firstView, firstView + ViewCount())
        bool originTopLeft = false;        // !bgfx::getCaps()->originBottomLeft
        bool homogeneousDepth = true;      // bgfx::getCaps()->homogeneousDepth (GL style clip depth)
        int threads = 3;                   // shader conversion worker count
        // Decodes PNG/JPEG/TGA bytes to RGBA8 (used for noise and custom pack textures).
        std::function<bool(const std::string& bytes, int& width, int& height, std::vector<uint8_t>& rgba)> decodeImage;
    };

    // Vertex attributes a draw provides, as a bit per bgfx::Attrib.
    constexpr uint32_t AttribBit(int attrib) { return 1u << attrib; }

    // What kind of work a program does inside the frame. Decides view slot, framebuffer and flips.
    enum class Slot {
        Shadow, Begin, Sky, Terrain, Block, Entity, Misc, HandSolid, Deferred, Translucent, Clouds, Weather, HandTranslucent, Composite, Final
    };

    struct DrawResult {
        bool ok = false;
        bgfx::ViewId view = 0;
        bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
        uint64_t state = 0;       // render state bits to OR in (blend, depth write, ...); cull bits are left to the caller
        bool flipWinding = false;  // the pack render target is vertically flipped: invert cull direction
    };

    struct EngineTextures {
        bgfx::TextureHandle blockAtlas = BGFX_INVALID_HANDLE;     // "gtexture", "tex", "texture"
        bgfx::TextureHandle lightmap = BGFX_INVALID_HANDLE;       // "lightmap"
        bgfx::TextureHandle normals = BGFX_INVALID_HANDLE;        // "normals"
        bgfx::TextureHandle specular = BGFX_INVALID_HANDLE;       // "specular"
        bgfx::TextureHandle overlay = BGFX_INVALID_HANDLE;        // "iris_overlay" (entity hurt flash)
    };

    class Pipeline {
    public:
        Pipeline(std::shared_ptr<ShaderPackInstance> pack, const PipelineHost& host);
        ~Pipeline();
        Pipeline(const Pipeline&) = delete;
        Pipeline& operator=(const Pipeline&) = delete;

        const PipelineConfig& Config() const { return config; }
        ShaderPackInstance& Pack() { return *pack; }
        bool Usable() const { return usable; }
        const std::string& Error() const { return error; }

        // Number of consecutive view ids the pipeline owns, and the ids in execution order.
        uint16_t ViewCount() const;
        void AppendViewOrder(std::vector<bgfx::ViewId>& out) const;

        void Resize(int width, int height);
        void SetEngineTextures(const EngineTextures& tex) { engine = tex; }

        // Starts a frame: evaluates uniforms, clears buffers, prepares views.
        void BeginFrame(const FrameState& state);

        // Queues the programs a draw uses so conversion happens in the background (call early, e.g. at load).
        void Prefetch(const std::string& program, uint32_t attribMask);

        // Looks up (and converts if needed) the program for a draw, sets uniforms/textures on the current encoder.
        // `baseState` is the state the caller wants; blend/depth overrides from shaders.properties are merged.
        DrawResult Prepare(Slot slot, const std::string& program, uint32_t attribMask, const PackDrawState& draw, uint64_t baseState);

        // Runs shadowcomp/prepare/deferred/composite/final. Call once per frame after all scene draws.
        // `backbuffer` is the framebuffer the final pass writes to (BGFX_INVALID_HANDLE = default backbuffer) and its size.
        void RunPostPasses(bgfx::FrameBufferHandle backbuffer, int outWidth, int outHeight, const bgfx::VertexLayout& quadLayout,
                           bool depthToBackbuffer = false);

        // The view that draws into the shadow map and its matrices must be provided by the caller.
        bgfx::ViewId ShadowView() const;
        int ShadowResolution() const { return config.shadowMapResolution; }

        // The texture the final composite wrote to (valid after RunPostPasses when the pack wrote no final pass).
        bgfx::TextureHandle SceneColor() const;
        // Has the pack any program that reads this standard sampler (diagnostics)?
        bool IsProgramAvailable(const std::string& program) const;

        // Vertex layout the pipeline expects for fullscreen passes (position + texcoord0).
        static bgfx::VertexLayout FullscreenLayout();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
        std::shared_ptr<ShaderPackInstance> pack;
        PipelineHost host;
        PipelineConfig config;
        EngineTextures engine;
        bool usable = false;
        std::string error;
    };
}
