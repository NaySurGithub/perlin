#include "util/SystemInfo.hpp"
#include "util/Env.hpp"
#include <cstdio>
#include "client/renderer/bgfx/RenderContext_bgfx.hpp"

#include "client/platform/AppPlatform.hpp"
#include "client/renderer/Tessellator.hpp"
#include "client/Options.hpp"
#include "client/resources/ShaderPack.hpp"
#include "util/Paths.hpp"

#include <bx/math.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace perlin {
    std::string RenderContext_bgfx::GpuVendor() const {
        const bgfx::Caps* caps = bgfx::getCaps();
        return caps ? sysinfo::VendorName(caps->vendorId) : std::string();
    }

    std::string RenderContext_bgfx::GpuDescription() const {
        const bgfx::Caps* caps = bgfx::getCaps();
        if (!caps)
            return {};
        const std::string name = sysinfo::GpuName(caps->vendorId, caps->deviceId);
        if (!name.empty())
            return name;
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "GPU %04x:%04x", caps->vendorId, caps->deviceId);
        return buffer;
    }

    namespace {
        constexpr bgfx::ViewId kViewHand = 2;
        constexpr bgfx::ViewId kViewGui = 3;
        constexpr bgfx::ViewId kViewEntity = 4;
        constexpr bgfx::ViewId kViewBlur = 5;
        constexpr bgfx::ViewId kViewBlurOut = 6;
        constexpr bgfx::ViewId kViewLate = 8;
        constexpr bgfx::ViewId kViewDoll = 7;
        constexpr uint32_t kPageVerts = 1u << 20;
        constexpr uint32_t kPageIdx = (kPageVerts / 4) * 6;

        std::string Env(const char* name) {
            const char* v = std::getenv(name);
            return v ? v : "";
        }

        void DownsampleAlphaAware(const uint8_t* src, const size_t idx[4], uint8_t* d) {
            unsigned sum[4] = {0, 0, 0, 0};
            unsigned n = 0, na = 0;
            for (int k = 0; k < 4; k++) {
                na += src[idx[k] + 3];
                if (src[idx[k] + 3] > 128) {
                    for (int c = 0; c < 4; c++)
                        sum[c] += src[idx[k] + c];
                    n++;
                }
            }
            if (n == 0) {
                for (int c = 0; c < 3; c++) {
                    unsigned total = 0;
                    for (int k = 0; k < 4; k++)
                        total += src[idx[k] + c];
                    d[c] = (uint8_t)((total + 2) / 4);
                }
                d[3] = (uint8_t)((na + 2) / 4);
            } else {
                for (int c = 0; c < 3; c++)
                    d[c] = (uint8_t)((sum[c] + n / 2) / n);
                d[3] = (uint8_t)((na + 2) / 4);
            }
        }
    }

    class RenderContext_bgfx::Callback : public bgfx::CallbackI {
    public:
        explicit Callback(RenderContext_bgfx* owner) : owner(owner) {}
        void fatal(const char* filePath, uint16_t line, bgfx::Fatal::Enum code, const char* str) override {
            std::cout << "[bgfx] FATAL " << filePath << ":" << line << " (" << (int)code << "): " << str
                      << std::endl;
            std::abort();
        }
        void traceVargs(const char* filePath, uint16_t line, const char* format, va_list args) override {
            if (!PERLIN_ENV("PERLIN_BGFX_TRACE"))
                return;
            char buf[2048];
            vsnprintf(buf, sizeof(buf), format, args);
            std::cout << "[bgfx] " << filePath << ":" << line << " " << buf;
        }
        void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
        void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
        void profilerEnd() override {}
        uint32_t cacheReadSize(uint64_t) override { return 0; }
        bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
        void cacheWrite(uint64_t, const void*, uint32_t) override {}
        void screenShot(const char* filePath, uint32_t w, uint32_t h, uint32_t pitch,
                        bgfx::TextureFormat::Enum, const void* data, uint32_t, bool yflip) override {
            owner->SaveScreenshotData(filePath, w, h, pitch, static_cast<const uint8_t*>(data), yflip);
        }
        void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
        void captureEnd() override {}
        void captureFrame(const void*, uint32_t) override {}

    private:
        RenderContext_bgfx* owner;
    };

    RenderContext_bgfx::RenderContext_bgfx() = default;
    RenderContext_bgfx::~RenderContext_bgfx() = default;

    uint32_t RenderContext_bgfx::ResetFlags() const {
        uint32_t flags = BGFX_RESET_NONE;
        if (vsync)
            flags |= BGFX_RESET_VSYNC;
        const std::string msaa = Env("PERLIN_MSAA");
        if (msaa == "2")
            flags |= BGFX_RESET_MSAA_X2;
        else if (msaa == "4")
            flags |= BGFX_RESET_MSAA_X4;
        else if (msaa == "8")
            flags |= BGFX_RESET_MSAA_X8;
        else if (msaa == "16")
            flags |= BGFX_RESET_MSAA_X16;
        return flags;
    }

    bool RenderContext_bgfx::Initialize(AppPlatform& p, const Options& options) {
        platform = &p;
        vsync = options.vsync;

        NativeWindowInfo native = platform->GetNativeWindowInfo();
        platform->GetFramebufferSize(width, height);

        callback = std::make_unique<Callback>(this);

        bgfx::Init init;
        std::string forced = Env("PERLIN_RENDERER");
        init.type = bgfx::RendererType::Count;
        if (forced == "vulkan")
            init.type = bgfx::RendererType::Vulkan;
        else if (forced == "opengl" || forced == "gl")
            init.type = bgfx::RendererType::OpenGL;
        else if (forced == "d3d11")
            init.type = bgfx::RendererType::Direct3D11;
        else if (forced == "d3d12")
            init.type = bgfx::RendererType::Direct3D12;
        else if (forced == "metal")
            init.type = bgfx::RendererType::Metal;

        ShaderPack& pack = ShaderPack::Instance();
        if (!options.shaderPack.empty()) {
            std::string error;
            if (!pack.Open(paths::DataDir() + "/shaderpacks", options.shaderPack, error))
                std::cout << "[shaders] " << error << " - using the built-in shaders" << std::endl;
        }
        if (pack.Active() && init.type == bgfx::RendererType::Count) {
#if defined(_WIN32)
            const bool native = pack.HasBackend("dxbc");
#elif defined(__APPLE__)
            const bool native = pack.HasBackend("metal");
#else
            const bool native = pack.HasBackend("spirv") && pack.HasBackend("glsl");
#endif
            if (!native) {
                if (pack.HasBackend("spirv"))
                    init.type = bgfx::RendererType::Vulkan;
                else if (pack.HasBackend("glsl"))
                    init.type = bgfx::RendererType::OpenGL;
            }
        }
        init.swapChain.nwh = native.window;
        init.swapChain.ndt = native.display;
        if (native.wayland)
            init.platformData.type = bgfx::NativeWindowHandleType::Wayland;
        init.swapChain.width = (uint32_t)width;
        init.swapChain.height = (uint32_t)height;
        init.reset = ResetFlags();
        init.limits.maxTransientVbSize = 48u << 20;
        init.limits.maxTransientIbSize = 24u << 20;
        init.callback = callback.get();
        if (!bgfx::init(init)) {
            std::cout << "[bgfx] init failed" << std::endl;
            return false;
        }
        initialized = true;
        caveCulling = !PERLIN_ENV("PERLIN_NO_CAVECULL");

        const bgfx::Caps* caps = bgfx::getCaps();
        homogeneousDepth = caps->homogeneousDepth;
        rendererName = bgfx::getRendererName(bgfx::getRendererType());
        std::cout << "[bgfx] renderer: " << bgfx::getRendererName(bgfx::getRendererType())
                  << (homogeneousDepth ? " (clip z -1..1)" : " (clip z 0..1)") << std::endl;

        layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Uint8, true)
            .end();
        static_assert(sizeof(Vertex) == 28, "Vertex layout must match the bgfx vertex layout");

        positionLayout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
        colorLayout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();
        textureLayout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        static_assert(sizeof(PositionVertex) == 12 && sizeof(ColorVertex) == 16 && sizeof(TextureVertex) == 20);

        auto upload = [](const auto& vertices, const bgfx::VertexLayout& vertexLayout) {
            return bgfx::createVertexBuffer(
                bgfx::copy(vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(vertices[0]))), vertexLayout);
        };
        auto uploadIndices = [](const std::vector<uint16_t>& indices) {
            return bgfx::createIndexBuffer(
                bgfx::copy(indices.data(), static_cast<uint32_t>(indices.size() * sizeof(uint16_t))));
        };
        const std::vector<PositionVertex> topSky = BuildSkyDisc(16.0f);
        topSkyBuffer = upload(topSky, positionLayout);
        bottomSkyBuffer = upload(BuildSkyDisc(-16.0f), positionLayout);
        {
            const std::vector<PositionVertex> screen = {{-1.0f, -1.0f, 0.0f}, {3.0f, -1.0f, 0.0f}, {-1.0f, 3.0f, 0.0f}};
            screenBuffer = upload(screen, positionLayout);
        }
        const std::vector<uint16_t> skyFan = FanIndices(static_cast<int>(topSky.size()));
        skyFanIndexCount = static_cast<uint32_t>(skyFan.size());
        skyFanIndices = uploadIndices(skyFan);

        const std::vector<ColorVertex> sunrise = BuildSunriseFan();
        sunriseBuffer = upload(sunrise, colorLayout);
        const std::vector<uint16_t> sunriseFan = FanIndices(static_cast<int>(sunrise.size()));
        sunriseIndexCount = static_cast<uint32_t>(sunriseFan.size());
        sunriseIndices = uploadIndices(sunriseFan);

        const std::vector<PositionVertex> stars = BuildStars();
        starBuffer = upload(stars, positionLayout);
        const std::vector<uint16_t> starQuads = QuadIndices(static_cast<int>(stars.size() / 4));
        starIndexCount = static_cast<uint32_t>(starQuads.size());
        starIndices = uploadIndices(starQuads);

        sunBuffer = upload(BuildSunQuad(), textureLayout);
        moonBuffer = upload(BuildMoonQuads(), textureLayout);
        quadIndices = uploadIndices(QuadIndices(1));

        const bool shadersOk = shaders.Initialize();
        if (shadersOk)
            InitShadows(options);
        if (!shadersOk) {
            std::cout << "[bgfx] shader program creation failed for "
                      << bgfx::getRendererName(bgfx::getRendererType()) << std::endl;
            return false;
        }

        samplerFlags = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

        std::string cull = Env("PERLIN_CULL");
        if (!cull.empty() && cull[0] == '0')
            cullState = 0;
        else if (!cull.empty() && (cull[0] == 'c' || cull[0] == 'C'))
            cullState = BGFX_STATE_CULL_CW;

        const uint32_t white = 0xFFFFFFFFu;
        const uint32_t whiteLayers[2] = {white, white};
        whiteTex = bgfx::createTexture2D(1, 1, false, 2, bgfx::TextureFormat::RGBA8, samplerFlags,
                                         bgfx::copy(whiteLayers, sizeof(whiteLayers)));

        lightmapFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        lightmapTex = bgfx::createTexture2D(kLightmapSize, kLightmapSize, false, 1,
                                            bgfx::TextureFormat::RGBA8, lightmapFlags, nullptr);

        bgfx::setViewMode(0, bgfx::ViewMode::Sequential);
        bgfx::setViewMode(kViewHand, bgfx::ViewMode::Sequential);
        bgfx::setViewMode(kViewEntity, bgfx::ViewMode::Sequential);
        bgfx::setViewMode(kViewLate, bgfx::ViewMode::Sequential);
        {
            const bgfx::ViewId order[9] = {0, 1, kViewEntity, kViewLate, kViewBlur, kViewBlurOut, kViewHand, kViewGui, kViewDoll};
            bgfx::setViewOrder(0, 9, order);
        }
        bgfx::setViewMode(kViewGui, bgfx::ViewMode::Sequential);
        bgfx::setViewMode(kViewDoll, bgfx::ViewMode::Sequential);
        bgfx::setViewMode(kViewBlur, bgfx::ViewMode::Sequential);
        bgfx::setViewMode(kViewBlurOut, bgfx::ViewMode::Sequential);

        uiLayout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        uiSampler = bgfx::createUniform("s_uiTex", bgfx::UniformType::Sampler);
        uiParams = bgfx::createUniform("u_uiParams", bgfx::UniformType::Vec4);
        uiOverlay = bgfx::createUniform("u_uiOverlay", bgfx::UniformType::Vec4);
        uiGlintSampler = bgfx::createUniform("s_uiGlint", bgfx::UniformType::Sampler);
        uiGlintA = bgfx::createUniform("u_uiGlintA", bgfx::UniformType::Vec4);
        uiGlintB = bgfx::createUniform("u_uiGlintB", bgfx::UniformType::Vec4);
        {
            const uint32_t whitePixel = 0xFFFFFFFFu;
            uiWhite = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                            BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT,
                                            bgfx::copy(&whitePixel, 4));
        }
        return true;
    }

    uint64_t RenderContext_bgfx::PassState(RenderLayer layer) const {
        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
        switch (layer) {
        case RenderLayer::Solid:
            state |= BGFX_STATE_WRITE_A | cullState;
            break;
        case RenderLayer::Cutout:
            state = (state & ~BGFX_STATE_DEPTH_TEST_MASK) | BGFX_STATE_DEPTH_TEST_LEQUAL;
            state |= BGFX_STATE_WRITE_A;
            break;
        case RenderLayer::CutoutCull:
            state = (state & ~BGFX_STATE_DEPTH_TEST_MASK) | BGFX_STATE_DEPTH_TEST_LEQUAL;
            state |= BGFX_STATE_WRITE_A | cullState;
            break;
        case RenderLayer::TranslucentCull:
            state |= cullState;
            state &= ~BGFX_STATE_WRITE_Z;
            [[fallthrough]];
        case RenderLayer::Translucent:
            state |=
                BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                               BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
            break;
        }
        return state;
    }

    void RenderContext_bgfx::UploadLightmap(const uint8_t* pixels) {
        if (lightmapValid && std::memcmp(lastLightmap, pixels, kLightmapBytes) == 0)
            return;
        std::memcpy(lastLightmap, pixels, kLightmapBytes);
        lightmapValid = true;
        bgfx::updateTexture2D(lightmapTex, 0, 0, 0, 0, kLightmapSize, kLightmapSize,
                              bgfx::copy(pixels, kLightmapBytes), kLightmapSize * 4);
    }

    void RenderContext_bgfx::Resize(int w, int h) {
        if (!initialized || w <= 0 || h <= 0)
            return;
        if (w == width && h == height)
            return;
        width = w;
        height = h;
        bgfx::SwapChain sc;
        sc.width = (uint32_t)w;
        sc.height = (uint32_t)h;
        bgfx::reset(ResetFlags(), &sc);
    }

    bool RenderContext_bgfx::EnsureMotionBlurTargets() {
        if (blur.targets)
            return true;
        const uint64_t sample = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        blur.worldColor = bgfx::createTexture2D(bgfx::BackbufferRatio::Equal, false, 1, bgfx::TextureFormat::RGBA8, sample);
        blur.worldDepth = bgfx::createTexture2D(bgfx::BackbufferRatio::Equal, false, 1, bgfx::TextureFormat::D24S8,
                                                BGFX_TEXTURE_RT_WRITE_ONLY);
        for (int i = 0; i < 2; i++)
            blur.histColor[i] = bgfx::createTexture2D(bgfx::BackbufferRatio::Equal, false, 1, bgfx::TextureFormat::RGBA8, sample);
        bool ok = bgfx::isValid(blur.worldColor) && bgfx::isValid(blur.worldDepth) && bgfx::isValid(blur.histColor[0]) &&
                  bgfx::isValid(blur.histColor[1]);
        if (ok) {
            const bgfx::TextureHandle world[2] = {blur.worldColor, blur.worldDepth};
            blur.worldFb = bgfx::createFrameBuffer(2, world, false);
            for (int i = 0; i < 2; i++) {
                const bgfx::TextureHandle h[1] = {blur.histColor[i]};
                blur.histFb[i] = bgfx::createFrameBuffer(1, h, false);
            }
            ok = bgfx::isValid(blur.worldFb) && bgfx::isValid(blur.histFb[0]) && bgfx::isValid(blur.histFb[1]);
        }
        if (!ok) {
            std::cout << "[blur] could not create the motion blur targets; disabled" << std::endl;
            DestroyMotionBlur();
            blur.targets = false;
            return false;
        }
        blur.sampCur = bgfx::createUniform("s_cur", bgfx::UniformType::Sampler);
        blur.sampHist = bgfx::createUniform("s_hist", bgfx::UniformType::Sampler);
        blur.params = bgfx::createUniform("u_post", bgfx::UniformType::Vec4);
        blur.targets = true;
        blur.valid = false;
        return true;
    }

    void RenderContext_bgfx::DestroyMotionBlur() {
        const auto kill = [](auto& h) {
            if (bgfx::isValid(h))
                bgfx::destroy(h);
            h = BGFX_INVALID_HANDLE;
        };
        kill(blur.worldFb);
        kill(blur.histFb[0]);
        kill(blur.histFb[1]);
        kill(blur.worldColor);
        kill(blur.worldDepth);
        kill(blur.histColor[0]);
        kill(blur.histColor[1]);
        kill(blur.sampCur);
        kill(blur.sampHist);
        kill(blur.params);
        blur.targets = false;
        blur.valid = false;
    }

    void RenderContext_bgfx::SetupMotionBlur(const FrameParams& params) {
        const float intensity = std::clamp(params.motionBlur, 0.0f, 1.0f);
        static bool failed = false;
        const bool on = intensity > 0.001f && !failed && !PERLIN_ENV("PERLIN_NO_BLUR");
        if (!on || !EnsureMotionBlurTargets()) {
            if (on && !blur.targets)
                failed = true;
            bgfx::setViewFrameBuffer(0, BGFX_INVALID_HANDLE);
            bgfx::setViewFrameBuffer(kViewEntity, BGFX_INVALID_HANDLE);
            bgfx::setViewFrameBuffer(kViewLate, BGFX_INVALID_HANDLE);
            blur.valid = false;
            if (blur.targets && !on)
                DestroyMotionBlur();
            return;
        }
        bgfx::setViewFrameBuffer(0, blur.worldFb);
        bgfx::setViewFrameBuffer(kViewEntity, blur.worldFb);
        bgfx::setViewFrameBuffer(kViewLate, blur.worldFb);

        const float now = params.timeSeconds;
        const float dt = std::clamp(now - blur.lastTime, 1.0f / 480.0f, 0.1f);
        blur.lastTime = now;
        float weight = std::pow(0.95f * intensity, dt * 60.0f);
        if (!blur.valid)
            weight = 0.0f;
        const int write = 1 - blur.read;
        const float origin = bgfx::getCaps()->originBottomLeft ? 1.0f : 0.0f;

        const auto fullscreen = [&](bgfx::ViewId view, bgfx::FrameBufferHandle fb, bgfx::TextureHandle cur,
                                    bgfx::TextureHandle hist, float w) {
            bgfx::setViewFrameBuffer(view, fb);
            bgfx::setViewRect(view, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
            const float p[4] = {w, origin, 0.0f, 0.0f};
            bgfx::setUniform(blur.params, p);
            bgfx::setTexture(0, blur.sampCur, cur, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
            bgfx::setTexture(1, blur.sampHist, hist, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
            bgfx::setVertexBuffer(0, screenBuffer);
            bgfx::setState(BGFX_STATE_WRITE_RGB);
            bgfx::submit(view, shaders.Program(ShaderProgram::Post));
        };
        fullscreen(kViewBlur, blur.histFb[write], blur.worldColor, blur.histColor[blur.read], weight);
        fullscreen(kViewBlurOut, BGFX_INVALID_HANDLE, blur.histColor[write], blur.histColor[write], 0.0f);
        blur.read = write;
        blur.valid = true;
    }

    void RenderContext_bgfx::BeginFrame(const FrameParams& params) {
        int w = 0, h = 0;
        platform->GetFramebufferSize(w, h);
        if (w > 0 && h > 0 && (w != width || h != height))
            Resize(w, h);

        auto quantize = [](float c) -> uint32_t {
            return static_cast<uint32_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        const uint32_t clear = (quantize(params.fogColor[0]) << 24) | (quantize(params.fogColor[1]) << 16) |
                               (quantize(params.fogColor[2]) << 8) | 0xFF;
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clear, 1.0f, 0);
        bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
        bgfx::touch(0);

        {
            float handProj[16];
            ConvertDepth(params.handProjection, handProj);
            static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            float worldProj[16];
            ConvertDepth(params.viewProjection, worldProj);
            bgfx::setViewRect(kViewEntity, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
            bgfx::setViewTransform(kViewEntity, identity, worldProj);
            bgfx::touch(kViewEntity);
            bgfx::setViewRect(kViewLate, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
            bgfx::touch(kViewLate);
            bgfx::setViewRect(kViewHand, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
            bgfx::setViewClear(kViewHand, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
            bgfx::setViewTransform(kViewHand, identity, handProj);
            bgfx::touch(kViewHand);
            float guiProj[16];
            const glm::mat4 ortho = glm::orthoRH_ZO(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f,
                                                    -1000.0f, 1000.0f);
            ConvertDepth(&ortho[0][0], guiProj);
            bgfx::setViewRect(kViewGui, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
            bgfx::setViewTransform(kViewGui, identity, guiProj);
            bgfx::touch(kViewGui);
            bgfx::setViewRect(kViewDoll, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
            bgfx::setViewClear(kViewDoll, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
            bgfx::setViewTransform(kViewDoll, identity, guiProj);
            bgfx::touch(kViewDoll);
        }
        SetupMotionBlur(params);

        std::memcpy(mvp, params.viewProjection, sizeof(mvp));
        if (homogeneousDepth) {
            for (int c = 0; c < 4; c++)
                mvp[c * 4 + 2] = 2.0f * params.viewProjection[c * 4 + 2] - params.viewProjection[c * 4 + 3];
        }
        shaders.SetFrame(params, tileSize);
        UploadLightmap(params.lightmap);
        frame = params;
        ConvertDepth(params.skyViewProjection, skyMvp);
    }

    void RenderContext_bgfx::ConvertDepth(const float in[16], float out[16]) const {
        std::memcpy(out, in, sizeof(float) * 16);
        if (homogeneousDepth)
            for (int c = 0; c < 4; c++)
                out[c * 4 + 2] = 2.0f * in[c * 4 + 2] - in[c * 4 + 3];
    }

    void RenderContext_bgfx::UploadSkyTextures(const Texture& sun, const Texture& moon, const Texture& clouds) {
        if (!initialized)
            return;
        auto create = [](const Texture& texture, uint64_t flags) {
            if (texture.width <= 0 || texture.height <= 0)
                return bgfx::TextureHandle(BGFX_INVALID_HANDLE);
            return bgfx::createTexture2D(static_cast<uint16_t>(texture.width), static_cast<uint16_t>(texture.height),
                                         false, 1, bgfx::TextureFormat::RGBA8, flags,
                                         bgfx::copy(texture.pixels.data(), static_cast<uint32_t>(texture.pixels.size())));
        };
        const uint64_t clamp = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP |
                               BGFX_SAMPLER_V_CLAMP;
        for (bgfx::TextureHandle* handle : {&sunTexture, &moonTexture})
            if (bgfx::isValid(*handle)) {
                bgfx::destroy(*handle);
                *handle = BGFX_INVALID_HANDLE;
            }
        sunTexture = create(sun, clamp);
        moonTexture = create(moon, clamp);
        cloudTexture.width = clouds.width;
        cloudTexture.height = clouds.height;
        cloudTexture.solid.assign(static_cast<size_t>(std::max(0, clouds.width)) * std::max(0, clouds.height), 0);
        if (clouds.pixels.size() >= cloudTexture.solid.size() * 4)
            for (size_t i = 0; i < cloudTexture.solid.size(); i++)
                cloudTexture.solid[i] = clouds.pixels[i * 4 + 3] >= 10 ? 1 : 0;
        cloudBuilt = false;
    }

    void RenderContext_bgfx::UploadCrosshair(const Texture& icons) {
        if (!initialized || icons.width < 16 || icons.height < 16)
            return;
        const int size = std::max(1, icons.width * 15 / 256);
        std::vector<uint8_t> crop((size_t)size * size * 4);
        for (int y = 0; y < size; y++)
            std::memcpy(crop.data() + (size_t)y * size * 4, icons.pixels.data() + (size_t)y * icons.width * 4,
                        (size_t)size * 4);
        if (bgfx::isValid(crosshairTexture))
            bgfx::destroy(crosshairTexture);
        const uint64_t clamp = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP |
                               BGFX_SAMPLER_V_CLAMP;
        crosshairTexture = bgfx::createTexture2D((uint16_t)size, (uint16_t)size, false, 1,
                                                 bgfx::TextureFormat::RGBA8, clamp,
                                                 bgfx::copy(crop.data(), (uint32_t)crop.size()));
        crosshairPx = size;
    }

    void RenderContext_bgfx::DrawCrosshair() {
    }

    void RenderContext_bgfx::DrawHudCursor(float x, float y, float w, float h) {
        if (!bgfx::isValid(crosshairTexture) || width <= 0 || height <= 0 || bgfx::getAvailTransientVertexBuffer(4, textureLayout) < 4)
            return;
        const float W = static_cast<float>(width), H = static_cast<float>(height);
        const auto nx = [&](float px) { return px / W * 2.0f - 1.0f; };
        const auto ny = [&](float py) { return 1.0f - py / H * 2.0f; };
        const float x0 = nx(std::round(x)), x1 = nx(std::round(x + w)), y0 = ny(std::round(y)), y1 = ny(std::round(y + h));
        bgfx::TransientVertexBuffer tvb;
        bgfx::allocTransientVertexBuffer(&tvb, 4, textureLayout);
        auto* v = reinterpret_cast<TextureVertex*>(tvb.data);
        v[0] = {x0, y1, 0.0f, 0.0f, 0.0f};
        v[1] = {x1, y1, 0.0f, 1.0f, 0.0f};
        v[2] = {x1, y0, 0.0f, 1.0f, 1.0f};
        v[3] = {x0, y0, 0.0f, 0.0f, 1.0f};
        static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        shaders.SetMatrix(identity);
        shaders.SetModulator(1.0f, 1.0f, 1.0f, 1.0f);
        shaders.BindSky(crosshairTexture, BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP |
                                              BGFX_SAMPLER_V_CLAMP);
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(quadIndices);
        bgfx::setState(BGFX_STATE_WRITE_RGB |
                       BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_INV_DST_COLOR, BGFX_STATE_BLEND_INV_SRC_COLOR,
                                                      BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ZERO));
        bgfx::submit(kViewGui, shaders.Program(ShaderProgram::Celestial));
    }

    uint32_t RenderContext_bgfx::UiCreateTexture(int w, int h, const uint8_t* rgba, bool linear, bool repeat) {
        if (!initialized || w <= 0 || h <= 0 || !rgba)
            return 0;
        uint64_t flags = repeat ? 0 : (BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        if (!linear)
            flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
        const bgfx::TextureHandle handle = bgfx::createTexture2D(
            static_cast<uint16_t>(w), static_cast<uint16_t>(h), false, 1, bgfx::TextureFormat::RGBA8, flags,
            bgfx::copy(rgba, static_cast<uint32_t>(w) * static_cast<uint32_t>(h) * 4));
        if (!bgfx::isValid(handle))
            return 0;
        for (size_t i = 0; i < uiTextures.size(); i++)
            if (!uiTextures[i].used) {
                uiTextures[i] = {handle, linear, true};
                return static_cast<uint32_t>(i + 1);
            }
        uiTextures.push_back({handle, linear, true});
        return static_cast<uint32_t>(uiTextures.size());
    }

    void RenderContext_bgfx::UiDestroyTexture(uint32_t id) {
        if (id == 0 || id > uiTextures.size() || !uiTextures[id - 1].used)
            return;
        bgfx::destroy(uiTextures[id - 1].handle);
        uiTextures[id - 1] = {};
    }

    uint32_t RenderContext_bgfx::UiCreateMesh(const UiVertex* vertices, uint32_t vertexCount, const uint32_t* indices,
                                              uint32_t indexCount) {
        if (!initialized || vertexCount == 0 || indexCount == 0)
            return 0;
        UiMeshGpu mesh;
        mesh.vb = bgfx::createVertexBuffer(bgfx::copy(vertices, vertexCount * sizeof(UiVertex)), uiLayout);
        mesh.ib = bgfx::createIndexBuffer(bgfx::copy(indices, indexCount * sizeof(uint32_t)), BGFX_BUFFER_INDEX32);
        mesh.indexCount = indexCount;
        mesh.used = true;
        if (!freeUiMeshes.empty()) {
            const uint32_t slot = freeUiMeshes.back();
            freeUiMeshes.pop_back();
            uiMeshes[slot] = mesh;
            return slot + 1;
        }
        uiMeshes.push_back(mesh);
        return static_cast<uint32_t>(uiMeshes.size());
    }

    uint32_t RenderContext_bgfx::UiCreateFrameMesh(const UiVertex* vertices, uint32_t vertexCount, const uint32_t* indices,
                                                   uint32_t indexCount) {
        if (!initialized || vertexCount == 0 || indexCount == 0)
            return 0;
        if (bgfx::getAvailTransientVertexBuffer(vertexCount, uiLayout) < vertexCount ||
            bgfx::getAvailTransientIndexBuffer(indexCount, true) < indexCount)
            return UiCreateMesh(vertices, vertexCount, indices, indexCount);
        UiMeshGpu mesh;
        bgfx::allocTransientVertexBuffer(&mesh.tvb, vertexCount, uiLayout);
        bgfx::allocTransientIndexBuffer(&mesh.tib, indexCount, true);
        std::memcpy(mesh.tvb.data, vertices, static_cast<size_t>(vertexCount) * sizeof(UiVertex));
        std::memcpy(mesh.tib.data, indices, static_cast<size_t>(indexCount) * sizeof(uint32_t));
        mesh.indexCount = indexCount;
        mesh.used = true;
        mesh.transient = true;
        if (!freeUiMeshes.empty()) {
            const uint32_t slot = freeUiMeshes.back();
            freeUiMeshes.pop_back();
            uiMeshes[slot] = mesh;
            return slot + 1;
        }
        uiMeshes.push_back(mesh);
        return static_cast<uint32_t>(uiMeshes.size());
    }

    void RenderContext_bgfx::UiDestroyMesh(uint32_t id) {
        if (id == 0 || id > uiMeshes.size() || !uiMeshes[id - 1].used)
            return;
        if (!uiMeshes[id - 1].transient) {
            bgfx::destroy(uiMeshes[id - 1].vb);
            bgfx::destroy(uiMeshes[id - 1].ib);
        }
        uiMeshes[id - 1] = {};
        freeUiMeshes.push_back(id - 1);
    }

    void RenderContext_bgfx::UiSubmit(UiLayer layer, const UiDraw& draw) {
        if (!initialized || draw.mesh == 0 || draw.mesh > uiMeshes.size() || !uiMeshes[draw.mesh - 1].used)
            return;
        const UiMeshGpu& mesh = uiMeshes[draw.mesh - 1];
        bgfx::TextureHandle texture = uiWhite;
        if (draw.texture != 0 && draw.texture <= uiTextures.size() && uiTextures[draw.texture - 1].used)
            texture = uiTextures[draw.texture - 1].handle;
        const bgfx::ViewId view = layer == UiLayer::World ? kViewEntity : layer == UiLayer::Hand ? kViewHand : layer == UiLayer::Doll ? kViewDoll : kViewGui;

        float model[16];
        std::memcpy(model, draw.model, sizeof(model));
        bgfx::setTransform(model);
        if (mesh.transient) {
            bgfx::setVertexBuffer(0, &mesh.tvb);
            bgfx::setIndexBuffer(&mesh.tib);
        } else {
            bgfx::setVertexBuffer(0, mesh.vb);
            bgfx::setIndexBuffer(mesh.ib);
        }
        bgfx::setTexture(0, uiSampler, texture);
        const bool glint = draw.glintTexture != 0 && draw.glintTexture <= uiTextures.size() && uiTextures[draw.glintTexture - 1].used;
        const float params[4] = {draw.alphaCutoff, glint ? 1.0f : 0.0f, draw.glintStrength, static_cast<float>(draw.emissive)};
        bgfx::setUniform(uiParams, params);
        bgfx::setUniform(uiOverlay, draw.overlay);
        bgfx::setTexture(1, uiGlintSampler, glint ? uiTextures[draw.glintTexture - 1].handle : uiWhite);
        const float glintA[4] = {draw.glintMatrix[0], draw.glintMatrix[1], draw.glintMatrix[2], draw.glintMatrix[3]};
        const float glintB[4] = {draw.glintOffset[0], draw.glintOffset[1], 0, 0};
        bgfx::setUniform(uiGlintA, glintA);
        bgfx::setUniform(uiGlintB, glintB);
        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
        if (glint) {
            state = BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
            if (draw.depth)
                state |= BGFX_STATE_DEPTH_TEST_LEQUAL;
        } else if (draw.premultiplied)
            state |= BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                                    BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
        else if (draw.blend == 1)
            state |= BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                                    BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
        else if (draw.blend == 2)
            state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
        if (draw.depth && !glint) {
            state |= BGFX_STATE_DEPTH_TEST_LEQUAL;
            if (draw.depthWrite)
                state |= BGFX_STATE_WRITE_Z;
        }
        bgfx::setState(state);
        if (draw.scissor)
            bgfx::setScissor(static_cast<uint16_t>(std::max(0, draw.scissorRect[0])),
                             static_cast<uint16_t>(std::max(0, draw.scissorRect[1])),
                             static_cast<uint16_t>(std::max(0, draw.scissorRect[2])),
                             static_cast<uint16_t>(std::max(0, draw.scissorRect[3])));
        bgfx::submit(view, shaders.Program(ShaderProgram::Ui));
    }

    void RenderContext_bgfx::DestroyUi() {
        for (size_t i = 0; i < uiMeshes.size(); i++)
            UiDestroyMesh(static_cast<uint32_t>(i + 1));
        for (size_t i = 0; i < uiTextures.size(); i++)
            UiDestroyTexture(static_cast<uint32_t>(i + 1));
        uiMeshes.clear();
        freeUiMeshes.clear();
        uiTextures.clear();
        if (bgfx::isValid(uiWhite)) bgfx::destroy(uiWhite);
        if (bgfx::isValid(uiSampler)) bgfx::destroy(uiSampler);
        if (bgfx::isValid(uiParams)) bgfx::destroy(uiParams);
        if (bgfx::isValid(uiOverlay)) bgfx::destroy(uiOverlay);
        if (bgfx::isValid(uiGlintSampler)) bgfx::destroy(uiGlintSampler);
        if (bgfx::isValid(uiGlintA)) bgfx::destroy(uiGlintA);
        if (bgfx::isValid(uiGlintB)) bgfx::destroy(uiGlintB);
        uiWhite = BGFX_INVALID_HANDLE;
        uiSampler = BGFX_INVALID_HANDLE;
        uiParams = BGFX_INVALID_HANDLE;
        uiOverlay = BGFX_INVALID_HANDLE;
        uiGlintSampler = uiGlintA = uiGlintB = BGFX_INVALID_HANDLE;
    }

    void RenderContext_bgfx::EndFrame() {
        bgfx::frame();
    }

    void RenderContext_bgfx::RequestScreenshot(const std::string& path) {
        if (!initialized)
            return;
        bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path.c_str());
    }

    void RenderContext_bgfx::SaveScreenshotData(const std::string& path, uint32_t w, uint32_t h,
                                                uint32_t pitch, const uint8_t* bgra, bool yflip) {
        FILE* f = fopen(path.c_str(), "wb");
        if (!f)
            return;
        uint32_t rowBytes = w * 4;
        uint32_t fileSize = 14 + 40 + rowBytes * h;
        uint8_t fh[14] = {'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0};
        std::memcpy(fh + 2, &fileSize, 4);
        uint8_t ih[40] = {};
        uint32_t biSize = 40;
        std::memcpy(ih + 0, &biSize, 4);
        std::memcpy(ih + 4, &w, 4);
        int32_t negH = yflip ? (int32_t)h : -(int32_t)h;
        std::memcpy(ih + 8, &negH, 4);
        ih[12] = 1;
        ih[14] = 32;
        uint32_t imgSize = rowBytes * h;
        std::memcpy(ih + 20, &imgSize, 4);
        fwrite(fh, 1, 14, f);
        fwrite(ih, 1, 40, f);
        std::vector<uint8_t> row(rowBytes);
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t* src = bgra + (size_t)y * pitch;
            for (uint32_t x = 0; x < w; x++) {
                row[x * 4 + 0] = src[x * 4 + 0];
                row[x * 4 + 1] = src[x * 4 + 1];
                row[x * 4 + 2] = src[x * 4 + 2];
                row[x * 4 + 3] = 255;
            }
            fwrite(row.data(), 1, rowBytes, f);
        }
        fclose(f);
    }

    namespace {
        int MipCount(int size) {
            int n = 1;
            while ((size >> n) > 0)
                n++;
            return n;
        }

        std::vector<std::vector<uint8_t>> BuildTileMips(int kTileSize, const uint8_t* rgba, size_t stride) {
            const int kTileMips = MipCount(kTileSize);
            std::vector<std::vector<uint8_t>> mips;
            mips.reserve(static_cast<size_t>(kTileMips));
            std::vector<uint8_t> level((size_t)kTileSize * kTileSize * 4);
            bool anyClear = false;
            for (int y = 0; y < kTileSize; y++) {
                std::memcpy(level.data() + (size_t)y * kTileSize * 4, rgba + (size_t)y * stride, (size_t)kTileSize * 4);
                for (int x = 0; x < kTileSize; x++)
                    anyClear = anyClear || level[((size_t)y * kTileSize + x) * 4 + 3] == 0;
            }
            if (anyClear) {
                std::vector<uint8_t> bled = level;
                for (int y = 0; y < kTileSize; y++)
                    for (int x = 0; x < kTileSize; x++) {
                        uint8_t* px = bled.data() + ((size_t)y * kTileSize + x) * 4;
                        if (level[((size_t)y * kTileSize + x) * 4 + 3] != 0)
                            continue;
                        unsigned sum[3] = {0, 0, 0}, n = 0;
                        for (int dy = -1; dy <= 1; dy++)
                            for (int dx = -1; dx <= 1; dx++) {
                                const int nx = x + dx, ny = y + dy;
                                if (nx < 0 || ny < 0 || nx >= kTileSize || ny >= kTileSize)
                                    continue;
                                const uint8_t* q = level.data() + ((size_t)ny * kTileSize + nx) * 4;
                                if (!q[3])
                                    continue;
                                sum[0] += q[0];
                                sum[1] += q[1];
                                sum[2] += q[2];
                                n++;
                            }
                        if (n) {
                            px[0] = (uint8_t)(sum[0] / n);
                            px[1] = (uint8_t)(sum[1] / n);
                            px[2] = (uint8_t)(sum[2] / n);
                        }
                    }
                level = std::move(bled);
            }
            for (int l = 0; l < kTileMips; l++) {
                const int w = std::max(1, kTileSize >> l);
                if (l + 1 == kTileMips) {
                    mips.push_back(std::move(level));
                    break;
                }
                const int h = std::max(1, w >> 1);
                std::vector<uint8_t> next((size_t)h * h * 4);
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < h; x++) {
                        const size_t idx[4] = {
                            ((size_t)(y * 2) * w + x * 2) * 4,
                            ((size_t)(y * 2) * w + x * 2 + 1) * 4,
                            ((size_t)(y * 2 + 1) * w + x * 2) * 4,
                            ((size_t)(y * 2 + 1) * w + x * 2 + 1) * 4,
                        };
                        DownsampleAlphaAware(level.data(), idx, next.data() + ((size_t)y * h + x) * 4);
                    }
                mips.push_back(std::move(level));
                level = std::move(next);
            }
            return mips;
        }

        void SubmitTileMips(bgfx::TextureHandle tex, int kTileSize, int layer, const std::vector<std::vector<uint8_t>>& mips) {
            for (size_t l = 0; l < mips.size(); l++) {
                const int w = std::max(1, kTileSize >> static_cast<int>(l));
                bgfx::updateTexture2D(tex, (uint16_t)layer, (uint8_t)l, 0, 0, (uint16_t)w, (uint16_t)w,
                                      bgfx::copy(mips[l].data(), (uint32_t)((size_t)w * w * 4)), (uint16_t)(w * 4));
            }
        }

        void UploadTile(bgfx::TextureHandle tex, int kTileSize, int layer, const uint8_t* rgba, size_t stride) {
            SubmitTileMips(tex, kTileSize, layer, BuildTileMips(kTileSize, rgba, stride));
        }
    }

    void RenderContext_bgfx::UploadAtlas(const uint8_t* rgba, int tilePx, int tiles) {
        if (!initialized || !rgba || tilePx <= 0 || tiles <= 0)
            return;
        animatedTiles.clear();
        const int limit = std::max<int>(1, bgfx::getCaps()->limits.maxTextureLayers);
        const int layers = std::min(limit, (tiles + 255) / 256 * 256);
        bgfx::TextureHandle tex = bgfx::createTexture2D((uint16_t)tilePx, (uint16_t)tilePx, true, (uint16_t)layers,
                                                        bgfx::TextureFormat::RGBA8, samplerFlags, nullptr);
        if (!bgfx::isValid(tex)) {
            std::cout << "[bgfx] atlas upload failed (" << layers << " layers of " << tilePx << "px)" << std::endl;
            return;
        }
        const size_t stride = (size_t)tilePx * 4;
        const size_t tileBytes = (size_t)tilePx * tilePx * 4;
        const int count = std::min(tiles, layers);
        for (int tile = 0; tile < count; tile++)
            UploadTile(tex, tilePx, tile, rgba + tileBytes * (size_t)tile, stride);
        if (bgfx::isValid(atlasTex))
            bgfx::destroy(atlasTex);
        atlasTex = tex;
        atlasLayers = layers;
        tileSize = tilePx;
    }

    void RenderContext_bgfx::UpdateAtlasRegion(int tile, const uint8_t* rgba) {
        if (!initialized || !bgfx::isValid(atlasTex) || !rgba || tile < 0 || tile >= atlasLayers)
            return;
        auto it = animatedTiles.find(rgba);
        if (it == animatedTiles.end()) {
            if (animatedTiles.size() > 4096)
                animatedTiles.clear();
            it = animatedTiles.emplace(rgba, BuildTileMips(tileSize, rgba, (size_t)tileSize * 4)).first;
        }
        SubmitTileMips(atlasTex, tileSize, tile, it->second);
    }

    bool RenderContext_bgfx::CreatePage(uint32_t vCap, const std::array<uint32_t, kRenderLayerCount>& iCap) {
        Page p;
        for (auto& h : p.ib)
            h = BGFX_INVALID_HANDLE;
        p.vb = bgfx::createDynamicVertexBuffer(vCap, layout, BGFX_BUFFER_NONE);
        bool ok = bgfx::isValid(p.vb);
        for (int l = 0; l < kRenderLayerCount && ok; l++) {
            p.ib[l] = bgfx::createDynamicIndexBuffer(iCap[l], BGFX_BUFFER_INDEX32);
            ok = bgfx::isValid(p.ib[l]);
        }
        if (!ok) {
            if (bgfx::isValid(p.vb))
                bgfx::destroy(p.vb);
            for (auto& h : p.ib)
                if (bgfx::isValid(h))
                    bgfx::destroy(h);
            return false;
        }
        p.vAlloc.Reset(vCap);
        for (int l = 0; l < kRenderLayerCount; l++)
            p.iAlloc[l].Reset(iCap[l]);
        pages.push_back(std::move(p));
        return true;
    }

    bool RenderContext_bgfx::AllocMesh(uint32_t v, const std::array<uint32_t, kRenderLayerCount>& need, GpuChunk& out) {
        auto tryPage = [&](Page& pg, int index) {
            uint32_t vo = 0;
            if (!pg.vAlloc.Alloc(v, vo))
                return false;
            std::array<uint32_t, kRenderLayerCount> io{};
            for (int l = 0; l < kRenderLayerCount; l++) {
                if (need[l] && !pg.iAlloc[l].Alloc(need[l], io[l])) {
                    for (int k = 0; k < l; k++)
                        if (need[k])
                            pg.iAlloc[k].Free(io[k], need[k]);
                    pg.vAlloc.Free(vo, v);
                    return false;
                }
            }
            out.page = index;
            out.vOff = vo;
            out.iOff = io;
            return true;
        };
        for (size_t pi = 0; pi < pages.size(); pi++)
            if (tryPage(pages[pi], (int)pi))
                return true;
        constexpr uint32_t kBase = kPageIdx;
        std::array<uint32_t, kRenderLayerCount> caps = {kBase, kBase / 2, kBase / 2, kBase / 4, kBase / 8};
        for (int l = 0; l < kRenderLayerCount; l++)
            caps[l] = std::max(caps[l], need[l]);
        if (!CreatePage(std::max(kPageVerts, v), caps))
            return false;
        return tryPage(pages.back(), (int)pages.size() - 1);
    }

    void RenderContext_bgfx::FreeChunk(GpuChunk& g) {
        if (g.page >= 0 && g.page < (int)pages.size()) {
            pages[g.page].vAlloc.Free(g.vOff, g.vCount);
            for (int l = 0; l < kRenderLayerCount; l++)
                if (g.layerCount[l])
                    pages[g.page].iAlloc[l].Free(g.iOff[l], g.layerCount[l]);
        }
        g.page = -1;
    }

    void RenderContext_bgfx::DestroyPages() {
        for (Page& p : pages) {
            if (bgfx::isValid(p.vb))
                bgfx::destroy(p.vb);
            for (auto& h : p.ib)
                if (bgfx::isValid(h))
                    bgfx::destroy(h);
        }
        pages.clear();
    }

    void RenderContext_bgfx::UpdateChunkMesh(int cx, int cz, const Tessellator& tess, uint64_t version) {
        UploadMesh(gpuChunks, cx, cz, 16, tess, version);
    }

    void RenderContext_bgfx::UpdateLodTile(int tx, int tz, int size, const Tessellator& tess, uint64_t version) {
        UploadMesh(lodChunks, tx, tz, size, tess, version);
    }

    void RenderContext_bgfx::RemoveLodTile(int tx, int tz) {
        auto it = lodChunks.find(ChunkKey(tx, tz));
        if (it == lodChunks.end())
            return;
        FreeChunk(it->second);
        lodChunks.erase(it);
    }

    void RenderContext_bgfx::UploadMesh(std::unordered_map<int64_t, GpuChunk>& gpuChunks, int cx, int cz, int size,
                                        const Tessellator& tess, uint64_t version) {
        const bool lod = &gpuChunks == &lodChunks;
        int64_t key = ChunkKey(cx, cz);
        const auto& verts = tess.Vertices();
        auto it = gpuChunks.find(key);
        if (verts.empty() || tess.Empty()) {
            if (it != gpuChunks.end()) {
                FreeChunk(it->second);
                gpuChunks.erase(it);
                if (!lod)
                    meshGeneration++;
            }
            return;
        }
        if (it != gpuChunks.end() && it->second.version == version)
            return;
        if (it != gpuChunks.end()) {
            FreeChunk(it->second);
            gpuChunks.erase(it);
        }

        GpuChunk g;
        g.vCount = (uint32_t)verts.size();
        for (int l = 0; l < kRenderLayerCount; l++)
            g.layerCount[l] = (uint32_t)tess.Indices(static_cast<RenderLayer>(l)).size();
        if (!AllocMesh(g.vCount, g.layerCount, g))
            return;
        Page& pg = pages[g.page];

        bgfx::update(pg.vb, g.vOff, bgfx::copy(verts.data(), g.vCount * (uint32_t)sizeof(Vertex)));
        for (int l = 0; l < kRenderLayerCount; l++) {
            const auto& list = tess.Indices(static_cast<RenderLayer>(l));
            if (list.empty())
                continue;
            const bgfx::Memory* im = bgfx::alloc((uint32_t)(list.size() * sizeof(uint32_t)));
            uint32_t* dst = reinterpret_cast<uint32_t*>(im->data);
            for (size_t i = 0; i < list.size(); i++)
                dst[i] = list[i] + g.vOff;
            bgfx::update(pg.ib[l], g.iOff[l], im);
        }

        float lo = verts[0].y, hi = verts[0].y;
        for (const Vertex& v : verts) {
            lo = std::min(lo, v.y);
            hi = std::max(hi, v.y);
        }
        g.minY = lo;
        g.maxY = hi;

        const auto& secs = tess.Sections();
        if (secs.empty()) {
            GpuSection whole{lo, hi, 0, {}, {}};
            for (int l = 0; l < kRenderLayerCount; l++) {
                whole.start[l] = 0;
                whole.count[l] = g.layerCount[l];
            }
            g.sections.push_back(whole);
        } else {
            g.sections.reserve(secs.size());
            for (const MeshSection& ms : secs) {
                GpuSection section{(float)(ms.sy * 16), (float)(ms.sy * 16 + 16), ms.sy, {}, {}};
                for (int l = 0; l < kRenderLayerCount; l++) {
                    section.start[l] = ms.start[l];
                    section.count[l] = ms.count[l];
                }
                g.sections.push_back(section);
            }
        }
        g.cx = cx;
        g.cz = cz;
        g.size = size;
        g.version = version;
        {
            const auto& vis = tess.Visibility();
            if (!vis.empty() && vis.size() <= 32) {
                g.visMin = vis.front().sy;
                for (const SectionVis& v : vis)
                    g.conn.push_back(v.conn);
            }
        }
        if (!lod)
            meshGeneration++;
        gpuChunks[key] = std::move(g);
    }

    void RenderContext_bgfx::RemoveChunkMesh(int cx, int cz) {
        auto it = gpuChunks.find(ChunkKey(cx, cz));
        if (it == gpuChunks.end())
            return;
        FreeChunk(it->second);
        gpuChunks.erase(it);
        meshGeneration++;
    }

    namespace {
        glm::mat4 FromArray(const float m[16]) {
            glm::mat4 out;
            std::memcpy(&out[0][0], m, sizeof(float) * 16);
            return out;
        }

        constexpr uint64_t kOverlayBlend =
            BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE,
                                           BGFX_STATE_BLEND_ZERO);
        constexpr uint64_t kTranslucentBlend =
            BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                           BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    }

    void RenderContext_bgfx::DrawSky() {
        const SkyFrame& sky = frame.sky;
        shaders.ApplyFrame();
        if (!sky.enabled)
            return;

        const glm::mat4 view = FromArray(frame.skyViewProjection);
        float converted[16];
        auto submitMatrix = [&](const glm::mat4& m) {
            ConvertDepth(&m[0][0], converted);
            shaders.SetMatrix(converted);
        };
        constexpr float kHalfPi = 1.57079632679489661923f;
        const uint64_t write = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;

        submitMatrix(view);
        shaders.SetModulator(sky.skyColor[0], sky.skyColor[1], sky.skyColor[2], 1.0f);
        bgfx::setVertexBuffer(0, topSkyBuffer);
        bgfx::setIndexBuffer(skyFanIndices, 0, skyFanIndexCount);
        bgfx::setState(write);
        bgfx::submit(0, shaders.Program(ShaderProgram::Sky));

        if (sky.sunrise[3] > 0.001f) {
            glm::mat4 pose = glm::rotate(view, kHalfPi, glm::vec3(1.0f, 0.0f, 0.0f));
            const float flip = std::sin(sky.sunAngle) < 0.0f ? 3.14159265358979323846f : 0.0f;
            pose = glm::rotate(pose, flip + kHalfPi, glm::vec3(0.0f, 0.0f, 1.0f));
            pose = glm::scale(pose, glm::vec3(1.0f, 1.0f, sky.sunrise[3]));
            submitMatrix(pose);
            shaders.SetModulator(sky.sunrise[0], sky.sunrise[1], sky.sunrise[2], sky.sunrise[3]);
            bgfx::setVertexBuffer(0, sunriseBuffer);
            bgfx::setIndexBuffer(sunriseIndices, 0, sunriseIndexCount);
            bgfx::setState(write | kTranslucentBlend);
            bgfx::submit(0, shaders.Program(ShaderProgram::Sunrise));
        }

        const glm::mat4 celestial = glm::rotate(view, -kHalfPi, glm::vec3(0.0f, 1.0f, 0.0f));
        const uint64_t spriteFlags = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP |
                                     BGFX_SAMPLER_V_CLAMP;
        auto body = [&](float angle, float scale) {
            glm::mat4 pose = glm::rotate(celestial, angle, glm::vec3(1.0f, 0.0f, 0.0f));
            pose = glm::translate(pose, glm::vec3(0.0f, 100.0f, 0.0f));
            return glm::scale(pose, glm::vec3(scale, 1.0f, scale));
        };

        if (bgfx::isValid(sunTexture)) {
            submitMatrix(body(sky.sunAngle, 30.0f));
            shaders.SetModulator(1.0f, 1.0f, 1.0f, sky.rainBrightness);
            shaders.BindSky(sunTexture, spriteFlags);
            bgfx::setVertexBuffer(0, sunBuffer);
            bgfx::setIndexBuffer(quadIndices);
            bgfx::setState(write | kOverlayBlend);
            bgfx::submit(0, shaders.Program(ShaderProgram::Celestial));
        }
        if (bgfx::isValid(moonTexture)) {
            submitMatrix(body(sky.moonAngle, 20.0f));
            shaders.SetModulator(1.0f, 1.0f, 1.0f, sky.rainBrightness);
            shaders.BindSky(moonTexture, spriteFlags);
            bgfx::setVertexBuffer(0, moonBuffer, static_cast<uint32_t>(sky.moonPhase) * 4, 4);
            bgfx::setIndexBuffer(quadIndices);
            bgfx::setState(write | kOverlayBlend);
            bgfx::submit(0, shaders.Program(ShaderProgram::Celestial));
        }
        if (sky.starBrightness > 0.0f) {
            submitMatrix(glm::rotate(celestial, sky.starAngle, glm::vec3(1.0f, 0.0f, 0.0f)));
            shaders.SetModulator(sky.starBrightness, sky.starBrightness, sky.starBrightness, sky.starBrightness);
            bgfx::setVertexBuffer(0, starBuffer);
            bgfx::setIndexBuffer(starIndices, 0, starIndexCount);
            bgfx::setState(write | kOverlayBlend);
            bgfx::submit(0, shaders.Program(ShaderProgram::Star));
        }
        if (sky.darkDisc) {
            submitMatrix(glm::translate(view, glm::vec3(0.0f, 12.0f, 0.0f)));
            shaders.SetModulator(0.0f, 0.0f, 0.0f, 1.0f);
            bgfx::setVertexBuffer(0, bottomSkyBuffer);
            bgfx::setIndexBuffer(skyFanIndices, 0, skyFanIndexCount);
            bgfx::setState(write);
            bgfx::submit(0, shaders.Program(ShaderProgram::Sky));
        }
    }

    void RenderContext_bgfx::DrawClouds() {
        if (!ShaderPack::Instance().Flag("vanillaClouds", true))
            return;
        const CloudDraw& clouds = frame.cloud;
        if (!clouds.enabled || cloudTexture.solid.empty())
            return;

        const CloudFrame now = ComputeCloudFrame(frame.cameraPosition[0], frame.cameraPosition[1],
                                                 frame.cameraPosition[2], clouds.height, clouds.gameTicks, cloudTexture);
        const bool rebuild = !cloudBuilt || now.cellX != cloudBuiltFrame.cellX || now.cellZ != cloudBuiltFrame.cellZ ||
                             now.position != cloudBuiltFrame.position || clouds.fancy != cloudBuiltFancy ||
                             clouds.rangeChunks != cloudBuiltRange;
        if (rebuild) {
            const CloudMesh mesh = BuildCloudMesh(cloudTexture, now, clouds.fancy, clouds.rangeChunks);
            const uint32_t vertexCount = static_cast<uint32_t>(mesh.vertices.size());
            const uint32_t indexCount = static_cast<uint32_t>(mesh.indices.size());
            if (vertexCount > cloudVertexCapacity) {
                if (bgfx::isValid(cloudVertexBuffer))
                    bgfx::destroy(cloudVertexBuffer);
                cloudVertexCapacity = vertexCount + vertexCount / 4;
                cloudVertexBuffer = bgfx::createDynamicVertexBuffer(cloudVertexCapacity, textureLayout);
            }
            if (indexCount > cloudIndexCapacity) {
                if (bgfx::isValid(cloudIndexBuffer))
                    bgfx::destroy(cloudIndexBuffer);
                cloudIndexCapacity = indexCount + indexCount / 4;
                cloudIndexBuffer = bgfx::createDynamicIndexBuffer(cloudIndexCapacity, BGFX_BUFFER_INDEX32);
            }
            if (vertexCount && indexCount) {
                bgfx::update(cloudVertexBuffer, 0, bgfx::copy(mesh.vertices.data(), vertexCount * sizeof(TextureVertex)));
                bgfx::update(cloudIndexBuffer, 0, bgfx::copy(mesh.indices.data(), indexCount * sizeof(uint32_t)));
            }
            cloudIndexCount = vertexCount && indexCount ? indexCount : 0;
            cloudBuiltFrame = now;
            cloudBuiltFancy = clouds.fancy;
            cloudBuiltRange = clouds.rangeChunks;
            cloudBuilt = true;
        }
        if (!cloudIndexCount || !bgfx::isValid(cloudVertexBuffer) || !bgfx::isValid(cloudIndexBuffer))
            return;

        const float offset[3] = {-now.xInCell, now.relativeBottomY, -now.zInCell};
        shaders.SetCloud(offset, clouds.color);
        shaders.SetMatrix(skyMvp);
        bgfx::setVertexBuffer(0, cloudVertexBuffer);
        bgfx::setIndexBuffer(cloudIndexBuffer, 0, cloudIndexCount);
        uint64_t cull = 0;
        if (clouds.fancy)
            cull = cullState == BGFX_STATE_CULL_CCW ? BGFX_STATE_CULL_CW
                                                    : (cullState == BGFX_STATE_CULL_CW ? BGFX_STATE_CULL_CCW : 0);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LEQUAL | kTranslucentBlend |
                       cull);
        bgfx::submit(kViewLate, shaders.Program(ShaderProgram::Cloud));
    }

    void RenderContext_bgfx::UpdateCaveVisibility(float camX, float camY, float camZ) {
        const int ccx = (int)std::floor(camX / 16.0f), ccz = (int)std::floor(camZ / 16.0f);
        int csy = (int)std::floor(camY / 16.0f);
        if (caveGeneration == meshGeneration && caveCam[0] == ccx && caveCam[1] == csy && caveCam[2] == ccz)
            return;

        int minX = INT_MAX, maxX = INT_MIN, minZ = INT_MAX, maxZ = INT_MIN, minS = INT_MAX, maxS = INT_MIN;
        bool allData = true;
        for (auto& entry : gpuChunks) {
            GpuChunk& g = entry.second;
            g.secVis = ~0u;
            if (g.conn.empty()) {
                allData = false;
                continue;
            }
            minX = std::min(minX, g.cx);
            maxX = std::max(maxX, g.cx);
            minZ = std::min(minZ, g.cz);
            maxZ = std::max(maxZ, g.cz);
            minS = std::min(minS, g.visMin);
            maxS = std::max(maxS, g.visMin + (int)g.conn.size() - 1);
        }
        caveGeneration = meshGeneration;
        caveCam[0] = ccx;
        caveCam[1] = csy;
        caveCam[2] = ccz;
        if (!allData || minX > maxX || minS > maxS)
            return;
        minS = std::min(minS, csy);
        maxS = std::max(maxS, csy);
        if (ccx < minX || ccx > maxX || ccz < minZ || ccz > maxZ)
            return;
        const int W = maxX - minX + 1, D = maxZ - minZ + 1, H = maxS - minS + 1;
        if (H > 32)
            return;

        static std::vector<GpuChunk*> grid;
        static std::vector<uint8_t> entered;
        static std::vector<uint32_t> reached;
        grid.assign((size_t)W * D, nullptr);
        entered.assign((size_t)W * D * H, 0);
        reached.assign((size_t)W * D, 0);
        for (auto& entry : gpuChunks)
            grid[(size_t)(entry.second.cz - minZ) * W + (entry.second.cx - minX)] = &entry.second;

        static const int kDx[6] = {0, 0, 0, 0, -1, 1};
        static const int kDy[6] = {-1, 1, 0, 0, 0, 0};
        static const int kDz[6] = {0, 0, -1, 1, 0, 0};

        auto connected = [&](int x, int z, int s, int a, int b) {
            if (a == b)
                return false;
            if (a > b)
                std::swap(a, b);
            const GpuChunk* g = grid[(size_t)z * W + x];
            if (!g)
                return true;
            const int i = (s + minS) - g->visMin;
            if (i < 0 || i >= (int)g->conn.size())
                return true;
            return ((g->conn[i] >> (a * (11 - a) / 2 + (b - a - 1))) & 1) != 0;
        };

        const int sx = ccx - minX, sz = ccz - minZ, ss = csy - minS;
        reached[(size_t)sz * W + sx] |= 1u << ss;
        struct Move {
            int x, z, s;
            uint8_t dir, dirs;
        };
        static std::vector<Move> moves;
        moves.clear();
        for (int d = 0; d < 6; d++)
            moves.push_back({sx, sz, ss, (uint8_t)d, (uint8_t)(1u << d)});
        size_t head = 0;
        while (head < moves.size()) {
            const Move m = moves[head++];
            const int nx = m.x + kDx[m.dir], nz = m.z + kDz[m.dir], ns = m.s + kDy[m.dir];
            if (nx < 0 || nx >= W || nz < 0 || nz >= D || ns < 0 || ns >= H)
                continue;
            const int entry = m.dir ^ 1;
            uint8_t& done = entered[((size_t)nz * W + nx) * H + ns];
            if (done & (1u << entry))
                continue;
            done |= (uint8_t)(1u << entry);
            reached[(size_t)nz * W + nx] |= 1u << ns;
            for (int d = 0; d < 6; d++) {
                if (m.dirs & (1u << (d ^ 1)))
                    continue;
                if (!connected(nx, nz, ns, entry, d))
                    continue;
                moves.push_back({nx, nz, ns, (uint8_t)d, (uint8_t)(m.dirs | (1u << d))});
            }
        }
        for (int dz = -1; dz <= 1; dz++)
            for (int dx = -1; dx <= 1; dx++) {
                const int x = sx + dx, z = sz + dz;
                if (x < 0 || x >= W || z < 0 || z >= D)
                    continue;
                for (int ds = -1; ds <= 1; ds++)
                    if (ss + ds >= 0 && ss + ds < H)
                        reached[(size_t)z * W + x] |= 1u << (ss + ds);
            }
        for (auto& entry : gpuChunks) {
            GpuChunk& g = entry.second;
            const uint32_t col = reached[(size_t)(g.cz - minZ) * W + (g.cx - minX)];
            const int shift = g.visMin - minS;
            g.secVis = shift >= 0 ? (col >> shift) : (col << -shift);
        }
    }

    void RenderContext_bgfx::InitShadows(const Options& options) {
        shadow = ShadowState{};
        ShaderPack& pack = ShaderPack::Instance();
        if (!pack.Active() || !pack.Feature("shadows") || !options.shadows || PERLIN_ENV("PERLIN_NO_SHADOWS"))
            return;
        if (!shaders.Has(ShaderProgram::Shadow) || !shaders.Has(ShaderProgram::ShadowCutout)) {
            std::cout << "[shadows] the pack asks for shadows but has no shadow shaders for this backend" << std::endl;
            return;
        }
        const bgfx::Caps* caps = bgfx::getCaps();
        if (!(caps->formats[bgfx::TextureFormat::R32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)) {
            std::cout << "[shadows] float render targets are not supported; shadows off" << std::endl;
            return;
        }
        int size = static_cast<int>(pack.Number("shadowMapSize", 2048));
        size = std::clamp(size, 512, 4096);
        shadow.size = size;
        shadow.distance = options.shadowDistance > 0 ? static_cast<float>(options.shadowDistance)
                                                     : static_cast<float>(pack.Number("shadowDistance", 80));
        shadow.distance = std::clamp(shadow.distance, 24.0f, 256.0f);
        const uint64_t rt = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT |
                            BGFX_SAMPLER_MAG_POINT;
        shadow.color = bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::R32F, rt);
        bgfx::TextureHandle depth = bgfx::createTexture2D(size, size, false, 1, bgfx::TextureFormat::D24S8,
                                                          BGFX_TEXTURE_RT_WRITE_ONLY);
        bgfx::TextureHandle attachments[2] = {shadow.color, depth};
        shadow.fb = bgfx::createFrameBuffer(2, attachments, true);
        if (!bgfx::isValid(shadow.fb) || !bgfx::isValid(shadow.color)) {
            DestroyShadows();
            return;
        }
        shadow.enabled = true;
        std::cout << "[shadows] " << size << "x" << size << " map, " << shadow.distance << " blocks" << std::endl;
    }

    void RenderContext_bgfx::DestroyShadows() {
        if (bgfx::isValid(shadow.fb))
            bgfx::destroy(shadow.fb);
        shadow = ShadowState{};
    }

    void RenderContext_bgfx::RenderShadows(float camX, float camY, float camZ) {
        shadow.active = false;
        if (!shadow.enabled || !frame.sky.enabled)
            return;
        const float sunA = frame.sky.sunAngle;
        glm::vec3 L(-std::sin(sunA), std::cos(sunA), 0.0f);
        if (L.y < 0.0f)
            L = -L;
        if (L.y < 0.03f)
            return;
        const glm::vec3 fwd = -L;
        const glm::vec3 upRef = std::abs(fwd.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        const glm::vec3 right = glm::normalize(glm::cross(upRef, fwd));
        const glm::vec3 up = glm::cross(fwd, right);

        const float R = shadow.distance;
        const float D = R + 80.0f;
        const float texel = 2.0f * R / static_cast<float>(shadow.size);
        const float step = texel * 32.0f;
        const glm::vec3 cam(camX, camY, camZ);
        const float cx = std::floor(glm::dot(cam, right) / step) * step;
        const float cy = std::floor(glm::dot(cam, up) / step) * step;
        const float cz = std::floor(glm::dot(cam, fwd) / step) * step;

        float S[16] = {};
        auto col = [&](int c, int r) -> float& { return S[c * 4 + r]; };
        for (int j = 0; j < 3; j++) {
            col(j, 0) = right[j] / (2.0f * R);
            col(j, 1) = up[j] / (2.0f * R);
            col(j, 2) = fwd[j] / (2.0f * D);
        }
        col(3, 0) = 0.5f - cx / (2.0f * R);
        col(3, 1) = 0.5f - cy / (2.0f * R);
        col(3, 2) = 0.5f - cz / (2.0f * D);
        col(3, 3) = 1.0f;
        std::memcpy(shadow.sample, S, sizeof(S));

        const bgfx::Caps* caps = bgfx::getCaps();
        const float sy = caps->originBottomLeft ? 1.0f : -1.0f;
        const float sz = caps->homogeneousDepth ? 2.0f : 1.0f;
        const float tz = caps->homogeneousDepth ? -1.0f : 0.0f;
        float M[16];
        for (int c = 0; c < 4; c++) {
            const float u = shadow.sample[c * 4 + 0], v = shadow.sample[c * 4 + 1], d = shadow.sample[c * 4 + 2],
                        w = shadow.sample[c * 4 + 3];
            M[c * 4 + 0] = 2.0f * u - (c == 3 ? 1.0f : 0.0f);
            M[c * 4 + 1] = sy * (2.0f * v - (c == 3 ? 1.0f : 0.0f));
            M[c * 4 + 2] = sz * d + (c == 3 ? tz : 0.0f);
            M[c * 4 + 3] = w;
        }
        std::memcpy(shadow.raster, M, sizeof(M));
        shadow.info[0] = 1.0f / static_cast<float>(shadow.size);
        shadow.info[1] = 2.0f * R;
        shadow.info[2] = 2.0f * D;
        shadow.active = true;

        const int64_t key[6] = {static_cast<int64_t>(std::llround(cx / step)), static_cast<int64_t>(std::llround(cy / step)),
                                static_cast<int64_t>(std::llround(cz / step)),
                                static_cast<int64_t>(std::llround(L.x * 500.0f)),
                                static_cast<int64_t>(std::llround(L.y * 500.0f)),
                                static_cast<int64_t>(meshGeneration)};
        if (std::memcmp(key, shadow.key, sizeof(key)) == 0 && ++shadow.framesSince < 600)
            return;
        std::memcpy(shadow.key, key, sizeof(key));
        shadow.framesSince = 0;

        bgfx::setViewFrameBuffer(1, shadow.fb);
        bgfx::setViewRect(1, 0, 0, static_cast<uint16_t>(shadow.size), static_cast<uint16_t>(shadow.size));
        bgfx::setViewClear(1, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xFFFFFFFFu, 1.0f, 0);
        bgfx::touch(1);

        const bgfx::TextureHandle atlas = bgfx::isValid(atlasTex) ? atlasTex : whiteTex;
        const uint64_t state = BGFX_STATE_WRITE_R | BGFX_STATE_WRITE_G | BGFX_STATE_WRITE_B | BGFX_STATE_WRITE_A |
                               BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
        constexpr uint8_t kKeep = BGFX_DISCARD_INDEX_BUFFER | BGFX_DISCARD_VERTEX_STREAMS;
        const float reachXY = R * 1.0f, reachZ = D;
        struct Pass {
            RenderLayer layer;
            ShaderProgram program;
        };
        const Pass passes[3] = {{RenderLayer::Solid, ShaderProgram::Shadow},
                                {RenderLayer::Cutout, ShaderProgram::ShadowCutout},
                                {RenderLayer::CutoutCull, ShaderProgram::ShadowCutout}};
        for (const Pass& pass : passes) {
            const size_t slot = static_cast<size_t>(pass.layer);
            bool primed = false;
            for (const auto& entry : gpuChunks) {
                const GpuChunk& g = entry.second;
                if (g.page < 0 || !g.layerCount[slot])
                    continue;
                const glm::vec3 centre(g.cx * 16.0f + 8.0f, (g.minY + g.maxY) * 0.5f, g.cz * 16.0f + 8.0f);
                const float radius = std::sqrt(128.0f + 0.25f * (g.maxY - g.minY) * (g.maxY - g.minY));
                const glm::vec3 rel = centre - cam;
                if (std::abs(glm::dot(rel, right) - (cx - glm::dot(cam, right))) > reachXY + radius ||
                    std::abs(glm::dot(rel, up) - (cy - glm::dot(cam, up))) > reachXY + radius ||
                    std::abs(glm::dot(rel, fwd) - (cz - glm::dot(cam, fwd))) > reachZ + radius)
                    continue;
                if (!primed) {
                    shaders.SetMatrix(shadow.raster);
                    shaders.SetShadow(shadow.sample, shadow.info, true);
                    bgfx::setState(state);
                    shaders.Apply(atlas, lightmapTex, samplerFlags, lightmapFlags);
                    primed = true;
                }
                const Page& pg = pages[g.page];
                bgfx::setVertexBuffer(0, pg.vb);
                bgfx::setIndexBuffer(pg.ib[slot], g.iOff[slot], g.layerCount[slot]);
                bgfx::submit(1, shaders.Program(pass.program), 0, kKeep);
            }
            if (primed)
                bgfx::discard(BGFX_DISCARD_ALL);
        }
    }

    void RenderContext_bgfx::DrawLayer(RenderLayer layer, ShaderProgram program, const Frustum& frustum,
                                       const std::vector<VisibleChunk>& chunks, bgfx::TextureHandle atlas) {
        static const bool hideBlend = PERLIN_ENV("PERLIN_HIDE_TRANSLUCENT") != nullptr;
        if (hideBlend && (layer == RenderLayer::Translucent || layer == RenderLayer::TranslucentCull))
            return;
        const size_t slot = static_cast<size_t>(layer);
        const bool ordered = layer == RenderLayer::Translucent || layer == RenderLayer::TranslucentCull;
        const bool lateView = ordered;
        struct Run {
            int page;
            uint32_t start, count;
        };
        static thread_local std::vector<Run> runs;
        runs.clear();
        auto push = [&](int page, uint32_t start, uint32_t count) {
            if (!runs.empty() && runs.back().page == page && runs.back().start + runs.back().count == start)
                runs.back().count += count;
            else
                runs.push_back({page, start, count});
        };
        for (const VisibleChunk& item : chunks) {
            const GpuChunk& g = *item.chunk;
            if (!g.layerCount[slot])
                continue;
            const float x0 = g.cx * static_cast<float>(g.size), z0 = g.cz * static_cast<float>(g.size);
            const float extent = static_cast<float>(g.size);
            for (const GpuSection& sec : g.sections) {
                const uint32_t count = sec.count[slot];
                if (!count)
                    continue;
                if (g.secVis != ~0u) {
                    const int bit = sec.sy - g.visMin;
                    if (bit >= 0 && bit < 32 && !(g.secVis & (1u << bit)))
                        continue;
                }
                if (!item.fullyInside && !frustum.Visible(x0, sec.minY, z0, x0 + extent, sec.maxY, z0 + extent))
                    continue;
                push(g.page, g.iOff[slot] + sec.start[slot], count);
            }
        }
        if (runs.empty())
            return;
        if (!ordered) {
            std::sort(runs.begin(), runs.end(), [](const Run& a, const Run& b) {
                return a.page != b.page ? a.page < b.page : a.start < b.start;
            });
            size_t out = 0;
            for (const Run& r : runs) {
                if (out && runs[out - 1].page == r.page && runs[out - 1].start + runs[out - 1].count == r.start)
                    runs[out - 1].count += r.count;
                else
                    runs[out++] = r;
            }
            runs.resize(out);
        }
        drawCalls += static_cast<uint32_t>(runs.size());

        const uint64_t state = PassState(layer);
        constexpr uint8_t kKeep = BGFX_DISCARD_INDEX_BUFFER | BGFX_DISCARD_VERTEX_STREAMS;
        bool primed = false;
        for (const Run& r : runs) {
            const Page& pg = pages[r.page];
            bgfx::setVertexBuffer(0, pg.vb);
            bgfx::setIndexBuffer(pg.ib[slot], r.start, r.count);
            if (!primed) {
                const float off[4] = {0, 0, 0, 0};
                shaders.SetShadow(shadow.sample, shadow.active ? shadow.info : off, shadow.active);
                shaders.SetMatrix(mvp);
                bgfx::setState(state);
                shaders.Apply(atlas, lightmapTex, samplerFlags, lightmapFlags);
                if (shadow.active)
                    shaders.BindShadow(shadow.color);
                primed = true;
            }
            bgfx::submit(lateView ? kViewLate : 0, shaders.Program(program), 0, kKeep);
        }
        if (primed)
            bgfx::discard(BGFX_DISCARD_ALL);
    }

    void RenderContext_bgfx::DrawScreenTint() {
        const float* t = frame.screenTint;
        if (t[3] <= 0.001f || !bgfx::isValid(screenBuffer))
            return;
        shaders.SetModulator(t[0], t[1], t[2], t[3]);
        bgfx::setVertexBuffer(0, screenBuffer);
        bgfx::setState(BGFX_STATE_WRITE_RGB | kTranslucentBlend);
        bgfx::submit(kViewLate, shaders.Program(ShaderProgram::Screen));
    }

    void RenderContext_bgfx::DrawChunkMeshes(const Frustum& frustum, float camX, float camZ) {
        DrawSky();
        if (PERLIN_ENV("PERLIN_STATS")) {
            static uint32_t frames = 0;
            if (++frames % 240 == 0)
                std::printf("[draw] %u merged submits last frame, %zu chunks, %zu lod tiles, %zu pages\n", drawCalls,
                            gpuChunks.size(), lodChunks.size(), pages.size());
        }
        drawCalls = 0;
        if (gpuChunks.empty() && lodChunks.empty()) {
            DrawClouds();
            DrawScreenTint();
            DrawCrosshair();
            return;
        }

        if (caveCulling)
            UpdateCaveVisibility(frame.cameraPosition[0], frame.cameraPosition[1], frame.cameraPosition[2]);
        visible.clear();
        for (const auto& entry : gpuChunks) {
            const GpuChunk& g = entry.second;
            if (g.page < 0 || g.sections.empty())
                continue;
            const float x0 = g.cx * 16.0f, z0 = g.cz * 16.0f;
            const auto result = frustum.Test(x0, g.minY, z0, x0 + 16.0f, g.maxY, z0 + 16.0f);
            if (result == Frustum::Result::Outside)
                continue;
            const float dx = x0 + 8.0f - camX, dz = z0 + 8.0f - camZ;
            visible.push_back({dx * dx + dz * dz, &g, result == Frustum::Result::Inside});
        }
        for (const auto& entry : lodChunks) {
            const GpuChunk& g = entry.second;
            if (g.page < 0 || g.sections.empty())
                continue;
            const float s = static_cast<float>(g.size);
            const float x0 = g.cx * s, z0 = g.cz * s;
            const auto result = frustum.Test(x0, g.minY, z0, x0 + s, g.maxY, z0 + s);
            if (result == Frustum::Result::Outside)
                continue;
            const float dx = x0 + s * 0.5f - camX, dz = z0 + s * 0.5f - camZ;
            visible.push_back({dx * dx + dz * dz, &g, result == Frustum::Result::Inside});
        }

        const bgfx::TextureHandle atlas = bgfx::isValid(atlasTex) ? atlasTex : whiteTex;
        RenderShadows(frame.cameraPosition[0], frame.cameraPosition[1], frame.cameraPosition[2]);
        {
            const uint16_t order[2] = {1, 0};
            if (shadow.enabled)
                bgfx::setViewOrder(0, 2, order);
        }
        DrawLayer(RenderLayer::Solid, ShaderProgram::TerrainSolid, frustum, visible, atlas);
        DrawLayer(RenderLayer::Cutout, ShaderProgram::TerrainCutout, frustum, visible, atlas);
        DrawLayer(RenderLayer::CutoutCull, ShaderProgram::TerrainCutout, frustum, visible, atlas);
        std::sort(visible.begin(), visible.end(),
                  [](const VisibleChunk& a, const VisibleChunk& b) { return a.distance > b.distance; });
        DrawLayer(RenderLayer::Translucent, ShaderProgram::TerrainTranslucent, frustum, visible, atlas);
        DrawLayer(RenderLayer::TranslucentCull, ShaderProgram::TerrainTranslucent, frustum, visible, atlas);
        DrawClouds();
        DrawScreenTint();
        DrawCrosshair();
    }

    void RenderContext_bgfx::ClearChunkMeshes() {
        meshGeneration++;
        gpuChunks.clear();
        lodChunks.clear();
        DestroyPages();
    }

    void RenderContext_bgfx::DestroySkyResources() {
        for (bgfx::VertexBufferHandle* handle :
             {&topSkyBuffer, &bottomSkyBuffer, &screenBuffer, &starBuffer, &sunriseBuffer, &sunBuffer, &moonBuffer})
            if (bgfx::isValid(*handle)) {
                bgfx::destroy(*handle);
                *handle = BGFX_INVALID_HANDLE;
            }
        for (bgfx::IndexBufferHandle* handle : {&skyFanIndices, &sunriseIndices, &starIndices, &quadIndices})
            if (bgfx::isValid(*handle)) {
                bgfx::destroy(*handle);
                *handle = BGFX_INVALID_HANDLE;
            }
        for (bgfx::TextureHandle* handle : {&sunTexture, &moonTexture})
            if (bgfx::isValid(*handle)) {
                bgfx::destroy(*handle);
                *handle = BGFX_INVALID_HANDLE;
            }
        if (bgfx::isValid(cloudVertexBuffer))
            bgfx::destroy(cloudVertexBuffer);
        if (bgfx::isValid(cloudIndexBuffer))
            bgfx::destroy(cloudIndexBuffer);
        cloudVertexBuffer = BGFX_INVALID_HANDLE;
        cloudIndexBuffer = BGFX_INVALID_HANDLE;
        cloudVertexCapacity = cloudIndexCapacity = 0;
        cloudBuilt = false;
    }

    void RenderContext_bgfx::Shutdown() {
        if (!initialized)
            return;
        gpuChunks.clear();
        lodChunks.clear();
        DestroyPages();
        DestroyMotionBlur();
        DestroyShadows();
        DestroyUi();
        if (bgfx::isValid(atlasTex))
            bgfx::destroy(atlasTex);
        if (bgfx::isValid(whiteTex))
            bgfx::destroy(whiteTex);
        if (bgfx::isValid(lightmapTex))
            bgfx::destroy(lightmapTex);
        DestroySkyResources();
        shaders.Shutdown();
        atlasTex = whiteTex = lightmapTex = BGFX_INVALID_HANDLE;
        bgfx::shutdown();
        initialized = false;
    }
}
