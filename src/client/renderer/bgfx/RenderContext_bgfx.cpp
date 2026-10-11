#include "client/renderer/bgfx/RenderContext_bgfx.hpp"

#include "client/platform/AppPlatform.hpp"
#include "client/renderer/Tessellator.hpp"
#include "client/Options.hpp"
#include "util/Paths.hpp"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace perlin {
    namespace {
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
                d[0] = d[1] = d[2] = d[3] = 0;
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
            if (!std::getenv("PERLIN_BGFX_TRACE"))
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
        init.swapChain.nwh = native.window;
        init.swapChain.ndt = native.display;
        if (native.wayland)
            init.platformData.type = bgfx::NativeWindowHandleType::Wayland;
        init.swapChain.width = (uint32_t)width;
        init.swapChain.height = (uint32_t)height;
        init.reset = ResetFlags();
        init.callback = callback.get();
        if (!bgfx::init(init)) {
            std::cout << "[bgfx] init failed" << std::endl;
            return false;
        }
        initialized = true;

        const bgfx::Caps* caps = bgfx::getCaps();
        homogeneousDepth = caps->homogeneousDepth;
        std::cout << "[bgfx] renderer: " << bgfx::getRendererName(bgfx::getRendererType())
                  << (homogeneousDepth ? " (clip z -1..1)" : " (clip z 0..1)") << std::endl;

        layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Uint8, true)
            .end();
        static_assert(sizeof(Vertex) == 28, "Vertex layout must match the bgfx vertex layout");

        skyLayout.begin().add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float).end();
        static const float kSkyTriangle[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
        skyBuffer = bgfx::createVertexBuffer(bgfx::makeRef(kSkyTriangle, sizeof(kSkyTriangle)), skyLayout);

        std::string shaderDirectory;
        if (!options.shaderPack.empty())
            shaderDirectory = paths::DataDir() + "/shaderpacks/" + options.shaderPack;
        if (!shaders.Initialize(shaderDirectory)) {
            std::cout << "[bgfx] shader program creation failed for "
                      << bgfx::getRendererName(bgfx::getRendererType()) << std::endl;
            return false;
        }

        samplerFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MAG_POINT;
        std::string filter = Env("PERLIN_FILTER");
        if (!filter.empty() && (filter[0] == 'p' || filter[0] == 'P'))
            samplerFlags |= BGFX_SAMPLER_MIN_POINT;
        else if (!filter.empty() && (filter[0] == 'a' || filter[0] == 'A'))
            samplerFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_ANISOTROPIC |
                           BGFX_SAMPLER_MAG_ANISOTROPIC;

        std::string cull = Env("PERLIN_CULL");
        if (!cull.empty() && cull[0] == '0')
            cullState = 0;
        else if (!cull.empty() && (cull[0] == 'c' || cull[0] == 'C'))
            cullState = BGFX_STATE_CULL_CW;

        const uint32_t white = 0xFFFFFFFFu;
        whiteTex = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, samplerFlags,
                                         bgfx::copy(&white, sizeof(white)));

        lightmapFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT |
                        BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
        lightmapTex = bgfx::createTexture2D(kLightmapSize, kLightmapSize, false, 1,
                                            bgfx::TextureFormat::RGBA8, lightmapFlags, nullptr);

        bgfx::setViewMode(0, bgfx::ViewMode::Sequential);
        return true;
    }

    uint64_t RenderContext_bgfx::PassState(RenderLayer layer) const {
        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
        switch (layer) {
        case RenderLayer::Solid:
            state |= BGFX_STATE_WRITE_A | cullState;
            break;
        case RenderLayer::Cutout:
            state |= BGFX_STATE_WRITE_A;
            break;
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

        std::memcpy(mvp, params.viewProjection, sizeof(mvp));
        if (homogeneousDepth) {
            for (int c = 0; c < 4; c++)
                mvp[c * 4 + 2] = 2.0f * params.viewProjection[c * 4 + 2] - params.viewProjection[c * 4 + 3];
        }
        shaders.SetFrame(params, mvp);
        UploadLightmap(params.lightmap);
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

    void RenderContext_bgfx::UploadAtlas(const uint8_t* rgba, int size) {
        if (!initialized || !rgba || size <= 0)
            return;
        int levels = 1;
        for (int s = size; s > 1; s >>= 1)
            levels++;
        std::vector<std::vector<uint8_t>> mips((size_t)levels);
        mips[0].assign(rgba, rgba + (size_t)size * size * 4);
        for (int l = 1; l < levels; l++) {
            int pw = size >> (l - 1);
            int w = pw >> 1;
            mips[(size_t)l].resize((size_t)w * w * 4);
            const uint8_t* src = mips[(size_t)l - 1].data();
            uint8_t* dst = mips[(size_t)l].data();
            for (int y = 0; y < w; y++)
                for (int x = 0; x < w; x++) {
                    const size_t idx[4] = {
                        ((size_t)(y * 2) * pw + x * 2) * 4,
                        ((size_t)(y * 2) * pw + x * 2 + 1) * 4,
                        ((size_t)(y * 2 + 1) * pw + x * 2) * 4,
                        ((size_t)(y * 2 + 1) * pw + x * 2 + 1) * 4,
                    };
                    DownsampleAlphaAware(src, idx, dst + ((size_t)y * w + x) * 4);
                }
        }

        size_t total = 0;
        for (auto& m : mips)
            total += m.size();
        const bgfx::Memory* mem = bgfx::alloc((uint32_t)total);
        size_t off = 0;
        for (auto& m : mips) {
            std::memcpy(mem->data + off, m.data(), m.size());
            off += m.size();
        }
        bgfx::TextureHandle tex = bgfx::createTexture2D((uint16_t)size, (uint16_t)size, true, 1,
                                                        bgfx::TextureFormat::RGBA8, samplerFlags, mem);
        if (!bgfx::isValid(tex)) {
            std::cout << "[bgfx] atlas upload failed (size " << size << ")" << std::endl;
            return;
        }
        if (bgfx::isValid(atlasTex))
            bgfx::destroy(atlasTex);
        atlasTex = tex;
        atlasSize = size;
        atlasMips = std::move(mips);
    }

    void RenderContext_bgfx::UpdateAtlasRegion(int px, int py, const uint8_t* rgba16) {
        if (!initialized || !bgfx::isValid(atlasTex) || !rgba16 || px < 0 || py < 0)
            return;
        const int size = atlasSize;
        if (atlasMips.empty() || size <= 0 || px + 16 > size || py + 16 > size)
            return;
        bgfx::updateTexture2D(atlasTex, 0, 0, (uint16_t)px, (uint16_t)py, 16, 16,
                              bgfx::copy(rgba16, 16 * 16 * 4), 16 * 4);
        for (int y = 0; y < 16; y++)
            std::memcpy(atlasMips[0].data() + ((size_t)(py + y) * size + px) * 4, rgba16 + (size_t)y * 16 * 4,
                        16 * 4);
        const int levels = (int)atlasMips.size();
        std::vector<uint8_t> tmp;
        for (int l = 1; l < levels; l++) {
            int lw = std::max(size >> l, 1);
            int rx = px >> l, ry = py >> l;
            int rw = std::max(16 >> l, 1);
            int rh = rw;
            if (rx >= lw || ry >= lw)
                break;
            rw = std::min(rw, lw - rx);
            rh = std::min(rh, lw - ry);
            if (rw <= 0 || rh <= 0)
                break;
            tmp.assign((size_t)rw * rh * 4, 0);
            const uint8_t* src = atlasMips[(size_t)l - 1].data();
            uint8_t* dstCpu = atlasMips[(size_t)l].data();
            int pw = std::max(size >> (l - 1), 1);
            for (int y = 0; y < rh; y++)
                for (int x = 0; x < rw; x++) {
                    const size_t idx[4] = {
                        ((size_t)((ry + y) * 2) * pw + (rx + x) * 2) * 4,
                        ((size_t)((ry + y) * 2) * pw + (rx + x) * 2 + 1) * 4,
                        ((size_t)((ry + y) * 2 + 1) * pw + (rx + x) * 2) * 4,
                        ((size_t)((ry + y) * 2 + 1) * pw + (rx + x) * 2 + 1) * 4,
                    };
                    uint8_t* d = tmp.data() + ((size_t)y * rw + x) * 4;
                    DownsampleAlphaAware(src, idx, d);
                    std::memcpy(dstCpu + ((size_t)(ry + y) * lw + rx + x) * 4, d, 4);
                }
            bgfx::updateTexture2D(atlasTex, 0, (uint8_t)l, (uint16_t)rx, (uint16_t)ry, (uint16_t)rw,
                                  (uint16_t)rh, bgfx::copy(tmp.data(), (uint32_t)tmp.size()),
                                  (uint16_t)(rw * 4));
        }
    }

    bool RenderContext_bgfx::CreatePage(uint32_t vCap, uint32_t iCap) {
        Page p;
        p.vb = bgfx::createDynamicVertexBuffer(vCap, layout, BGFX_BUFFER_NONE);
        p.ib = bgfx::createDynamicIndexBuffer(iCap, BGFX_BUFFER_INDEX32);
        if (!bgfx::isValid(p.vb) || !bgfx::isValid(p.ib)) {
            if (bgfx::isValid(p.vb))
                bgfx::destroy(p.vb);
            if (bgfx::isValid(p.ib))
                bgfx::destroy(p.ib);
            return false;
        }
        p.vAlloc.Reset(vCap);
        p.iAlloc.Reset(iCap);
        pages.push_back(std::move(p));
        return true;
    }

    bool RenderContext_bgfx::AllocMesh(uint32_t v, uint32_t i, GpuChunk& out) {
        for (size_t pi = 0; pi < pages.size(); pi++) {
            uint32_t vo = 0, io = 0;
            if (!pages[pi].vAlloc.Alloc(v, vo))
                continue;
            if (!pages[pi].iAlloc.Alloc(i, io)) {
                pages[pi].vAlloc.Free(vo, v);
                continue;
            }
            out.page = (int)pi;
            out.vOff = vo;
            out.iOff = io;
            return true;
        }
        if (!CreatePage(std::max(kPageVerts, v), std::max(kPageIdx, i)))
            return false;
        Page& pg = pages.back();
        uint32_t vo = 0, io = 0;
        if (!pg.vAlloc.Alloc(v, vo) || !pg.iAlloc.Alloc(i, io))
            return false;
        out.page = (int)pages.size() - 1;
        out.vOff = vo;
        out.iOff = io;
        return true;
    }

    void RenderContext_bgfx::FreeChunk(GpuChunk& g) {
        if (g.page >= 0 && g.page < (int)pages.size()) {
            pages[g.page].vAlloc.Free(g.vOff, g.vCount);
            pages[g.page].iAlloc.Free(g.iOff, g.iCount);
        }
        g.page = -1;
    }

    void RenderContext_bgfx::DestroyPages() {
        for (Page& p : pages) {
            if (bgfx::isValid(p.vb))
                bgfx::destroy(p.vb);
            if (bgfx::isValid(p.ib))
                bgfx::destroy(p.ib);
        }
        pages.clear();
    }

    void RenderContext_bgfx::UpdateChunkMesh(int cx, int cz, const Tessellator& tess, uint64_t version) {
        int64_t key = ChunkKey(cx, cz);
        const auto& verts = tess.Vertices();
        auto it = gpuChunks.find(key);
        if (verts.empty() || tess.Empty()) {
            if (it != gpuChunks.end()) {
                FreeChunk(it->second);
                gpuChunks.erase(it);
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
        uint32_t total = 0;
        for (int l = 0; l < kRenderLayerCount; l++) {
            g.layerBase[l] = total;
            g.layerCount[l] = (uint32_t)tess.Indices(static_cast<RenderLayer>(l)).size();
            total += g.layerCount[l];
        }
        g.iCount = total;
        if (!AllocMesh(g.vCount, g.iCount, g))
            return;
        Page& pg = pages[g.page];

        bgfx::update(pg.vb, g.vOff, bgfx::copy(verts.data(), g.vCount * (uint32_t)sizeof(Vertex)));
        const bgfx::Memory* im = bgfx::alloc(g.iCount * (uint32_t)sizeof(uint32_t));
        for (int l = 0; l < kRenderLayerCount; l++) {
            const auto& list = tess.Indices(static_cast<RenderLayer>(l));
            std::memcpy(im->data + (size_t)g.layerBase[l] * sizeof(uint32_t), list.data(),
                        list.size() * sizeof(uint32_t));
        }
        bgfx::update(pg.ib, g.iOff, im);

        float lo = verts[0].y, hi = verts[0].y;
        for (const Vertex& v : verts) {
            lo = std::min(lo, v.y);
            hi = std::max(hi, v.y);
        }
        g.minY = lo;
        g.maxY = hi;

        const auto& secs = tess.Sections();
        if (secs.empty()) {
            GpuSection whole{lo, hi, {}, {}};
            for (int l = 0; l < kRenderLayerCount; l++) {
                whole.start[l] = g.layerBase[l];
                whole.count[l] = g.layerCount[l];
            }
            g.sections.push_back(whole);
        } else {
            g.sections.reserve(secs.size());
            for (const MeshSection& ms : secs) {
                GpuSection section{(float)(ms.sy * 16), (float)(ms.sy * 16 + 16), {}, {}};
                for (int l = 0; l < kRenderLayerCount; l++) {
                    section.start[l] = g.layerBase[l] + ms.start[l];
                    section.count[l] = ms.count[l];
                }
                g.sections.push_back(section);
            }
        }
        g.cx = cx;
        g.cz = cz;
        g.version = version;
        gpuChunks[key] = std::move(g);
    }

    void RenderContext_bgfx::RemoveChunkMesh(int cx, int cz) {
        auto it = gpuChunks.find(ChunkKey(cx, cz));
        if (it == gpuChunks.end())
            return;
        FreeChunk(it->second);
        gpuChunks.erase(it);
    }

    void RenderContext_bgfx::DrawSky() {
        const uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
        bgfx::setVertexBuffer(0, skyBuffer);
        bgfx::setState(state);
        shaders.Apply(bgfx::isValid(atlasTex) ? atlasTex : whiteTex, lightmapTex, samplerFlags,
                      lightmapFlags);
        bgfx::submit(0, shaders.Program(ShaderProgram::Sky));
    }

    void RenderContext_bgfx::DrawLayer(RenderLayer layer, ShaderProgram program, const Frustum& frustum,
                                       const std::vector<VisibleChunk>& chunks, bgfx::TextureHandle atlas) {
        const size_t slot = static_cast<size_t>(layer);
        const uint64_t state = PassState(layer);
        for (const VisibleChunk& item : chunks) {
            const GpuChunk& g = *item.chunk;
            if (!g.layerCount[slot])
                continue;
            const Page& pg = pages[g.page];
            const float x0 = g.cx * 16.0f, z0 = g.cz * 16.0f;
            uint32_t runStart = 0, runCount = 0;

            auto flush = [&]() {
                if (!runCount)
                    return;
                bgfx::setVertexBuffer(0, pg.vb, g.vOff, g.vCount);
                bgfx::setIndexBuffer(pg.ib, g.iOff + runStart, runCount);
                bgfx::setState(state);
                shaders.Apply(atlas, lightmapTex, samplerFlags, lightmapFlags);
                bgfx::submit(0, shaders.Program(program));
                runCount = 0;
            };

            for (const GpuSection& sec : g.sections) {
                const uint32_t count = sec.count[slot];
                if (!count)
                    continue;
                if (!item.fullyInside && !frustum.Visible(x0, sec.minY, z0, x0 + 16.0f, sec.maxY, z0 + 16.0f))
                    continue;
                const uint32_t start = sec.start[slot];
                if (runCount && runStart + runCount == start) {
                    runCount += count;
                } else {
                    flush();
                    runStart = start;
                    runCount = count;
                }
            }
            flush();
        }
    }

    void RenderContext_bgfx::DrawChunkMeshes(const Frustum& frustum, float camX, float camZ) {
        DrawSky();
        if (gpuChunks.empty())
            return;

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
        std::sort(visible.begin(), visible.end(),
                  [](const VisibleChunk& a, const VisibleChunk& b) { return a.distance < b.distance; });

        const bgfx::TextureHandle atlas = bgfx::isValid(atlasTex) ? atlasTex : whiteTex;
        DrawLayer(RenderLayer::Solid, ShaderProgram::TerrainSolid, frustum, visible, atlas);
        DrawLayer(RenderLayer::Cutout, ShaderProgram::TerrainCutout, frustum, visible, atlas);
        std::reverse(visible.begin(), visible.end());
        DrawLayer(RenderLayer::Translucent, ShaderProgram::TerrainTranslucent, frustum, visible, atlas);
    }

    void RenderContext_bgfx::ClearChunkMeshes() {
        gpuChunks.clear();
        DestroyPages();
    }

    void RenderContext_bgfx::Shutdown() {
        if (!initialized)
            return;
        gpuChunks.clear();
        DestroyPages();
        if (bgfx::isValid(atlasTex))
            bgfx::destroy(atlasTex);
        if (bgfx::isValid(whiteTex))
            bgfx::destroy(whiteTex);
        if (bgfx::isValid(lightmapTex))
            bgfx::destroy(lightmapTex);
        if (bgfx::isValid(skyBuffer))
            bgfx::destroy(skyBuffer);
        shaders.Shutdown();
        atlasTex = whiteTex = lightmapTex = BGFX_INVALID_HANDLE;
        skyBuffer = BGFX_INVALID_HANDLE;
        atlasMips.clear();
        bgfx::shutdown();
        initialized = false;
    }
}
