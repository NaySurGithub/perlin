#pragma once
#include "client/javashaders/Pipeline.hpp"
#include "client/renderer/RenderContext.hpp"
#include "client/renderer/Tessellator.hpp"

#include <bgfx/bgfx.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin {
    // Vertex format Java shader packs read. Parallel to the native terrain vertex (same vertex index).
    struct PackVertex {
        float x, y, z;            // Position
        uint32_t abgr;            // Color0
        float u, v;               // TexCoord0 (atlas uv)
        float lu, lv;             // TexCoord5 (block / sky light, 0..240)
        int8_t nx, ny, nz, nw;    // Normal
        float entity[2];          // TexCoord2: mc_Entity (block id, render type)
        float mid[2];             // TexCoord3: mc_midTexCoord
        int8_t midBlock[4];       // TexCoord4: at_midBlock (xyz in 1/64 block, w = emitted light)
        int8_t tangent[4];        // Tangent: at_tangent
    };
    static_assert(sizeof(PackVertex) == 60, "PackVertex layout");

    struct PackShadowSetup {
        float view[16];        // camera-relative shadow model view (what packs see as shadowModelView)
        float projection[16];  // GL style orthographic projection
        float drawView[16];    // view for absolute-position geometry (shadowModelView * translate(-camera))
        float lightDir[3];     // world space direction towards the active light
        int resolution = 2048;
        float distance = 128.0f;
    };

    // Owns a Java shader pack and everything between Perlin's renderer and the pack pipeline.
    class JavaPackHost {
    public:
        struct LoadConfig {
            std::string path;
            jshader::OptionValues options;
            jshader::BackendKind backend = jshader::BackendKind::OpenGL;
            jshader::HlslCompiler hlsl;
            bool originTopLeft = false;
            int width = 1280, height = 720;
            std::string dimension = "world0";
            std::function<std::string(uint16_t)> blockName;   // palette id -> "minecraft:stone"
        };

        JavaPackHost();
        ~JavaPackHost();

        bool Load(const LoadConfig& config, std::string& error);
        bool Active() const { return pipeline && pipeline->Usable(); }
        jshader::Pipeline& Pipe() { return *pipeline; }
        jshader::ShaderPackInstance& Pack() { return *instance; }
        const std::string& Name() const { return name; }
        void SetDimension(const std::string& folder);
        void Resize(int width, int height);

        // Atlas: tile-major RGBA data (tilePx*tilePx*4 bytes per tile), exactly what UploadAtlas receives.
        void SetAtlas(const uint8_t* rgba, int tilePx, int tiles);
        void UpdateTile(int tile, const uint8_t* rgba);
        bool HasAtlas() const { return bgfx::isValid(atlas); }
        bgfx::TextureHandle Atlas() const { return atlas; }

        static const bgfx::VertexLayout& Layout();
        static constexpr uint32_t AttribMask() {
            return jshader::AttribBit(jshader::kAttribPosition) | jshader::AttribBit(jshader::kAttribColor0) |
                   jshader::AttribBit(jshader::kAttribTexCoord0) | jshader::AttribBit(jshader::kAttribTexCoord5) |
                   jshader::AttribBit(jshader::kAttribNormal) | jshader::AttribBit(jshader::kAttribTexCoord2) |
                   jshader::AttribBit(jshader::kAttribTexCoord3) | jshader::AttribBit(jshader::kAttribTexCoord4) |
                   jshader::AttribBit(jshader::kAttribTangent);
        }
        // Converts a mesh to pack vertices (one per tessellator vertex).
        void Convert(const Tessellator& tess, std::vector<PackVertex>& out);

        // Fills the pipeline's frame state and shadow matrices from the renderer's frame description.
        void BuildFrame(const FrameParams& params, jshader::FrameState& state, PackShadowSetup& shadow, bool shadowsOn);
        jshader::PackDrawState TerrainDraw(const FrameParams& params, const jshader::FrameState& state) const;

        int PackBlockId(uint16_t paletteId);

    private:
        void BuildBlockMap();
        std::shared_ptr<jshader::ShaderPackInstance> instance;
        std::unique_ptr<jshader::Pipeline> pipeline;
        std::string name;
        std::function<std::string(uint16_t)> blockName;
        std::unordered_map<std::string, int> nameToPackId;
        std::vector<int16_t> paletteMap;           // palette id -> pack id (-2 = unresolved)
        bgfx::TextureHandle atlas = BGFX_INVALID_HANDLE;
        int atlasTilePx = 16, atlasTiles = 0, gridCols = 1, gridRows = 1;
        int atlasWidth = 0;
        int atlasLevels = 1;
        bool originTopLeft = false;
        // history for the previous-frame uniforms
        float prevModelView[16], prevProjection[16];
        double prevCamera[3] = {0, 0, 0};
        bool havePrev = false;
        uint32_t lastFrameIndex = 0;
        float lastTime = 0.0f;
        float gridWidthPx = 0;
    };
}
