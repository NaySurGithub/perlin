#pragma once
#include "client/javashaders/Transform.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin::jshader {
    // Everything the engine knows about the current frame. Matrices are column-major, GL conventions (what shader
    // packs expect), vectors in view space unless the name says otherwise.
    struct FrameState {
        float modelView[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};   // gbufferModelView (rotation only)
        float projection[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // gbufferProjection (no jitter-free copy kept)
        float prevModelView[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        float prevProjection[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        float shadowModelView[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        float shadowProjection[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        double cameraPosition[3] = {0, 0, 0};
        double previousCameraPosition[3] = {0, 0, 0};
        float sunPosition[3] = {0, 100, 0}, moonPosition[3] = {0, -100, 0}, shadowLightPosition[3] = {0, 100, 0};
        float upPosition[3] = {0, 100, 0};
        float sunAngle = 0.0f, shadowAngle = 0.0f;
        float fogColor[3] = {0.7f, 0.8f, 1.0f}, skyColor[3] = {0.5f, 0.7f, 1.0f};
        float fogDensity = 0.0f, fogStart = 0.0f, fogEnd = 128.0f;
        int fogMode = 0, fogShape = 0;
        int worldTime = 0, worldDay = 0, moonPhase = 0, frameCounter = 0;
        float frameTime = 0.016f, frameTimeCounter = 0.0f;
        float rainStrength = 0.0f, wetness = 0.0f, thunderStrength = 0.0f;
        int viewWidth = 1280, viewHeight = 720;
        float nearPlane = 0.05f, farPlane = 256.0f;
        int isEyeInWater = 0;
        float eyeAltitude = 64.0f;
        int eyeBrightness[2] = {240, 240}, eyeBrightnessSmooth[2] = {240, 240};
        float blindness = 0.0f, nightVision = 0.0f, screenBrightness = 0.5f, playerMood = 0.0f;
        int heldItemId = 0, heldItemId2 = 0, heldBlockLightValue = 0, heldBlockLightValue2 = 0;
        bool hideGUI = false;
        float centerDepthSmooth = 0.5f;
        float entityColor[4] = {0, 0, 0, 0};
        int entityId = 0, blockEntityId = 0, currentRenderedItemId = 0;
        int renderStage = 0;
        int atlasSize[2] = {0, 0};
        float biomeTemperature = 0.8f, biomeRainfall = 0.4f;
        int biome = 0, biomeCategory = 0, biomePrecipitation = 0;
        float playerHealth = 20.0f, maxPlayerHealth = 20.0f, playerHunger = 20.0f, playerAir = 300.0f, maxPlayerAir = 300.0f;
        bool isSpectator = false, isHurt = false, isInvisible = false, isBurning = false, isSneaking = false, isSprinting = false;
        bool isOnGround = true, isRightHanded = true;
        float cameraYaw = 0.0f, cameraPitch = 0.0f;
        float lightningBoltPosition[4] = {0, 0, 0, 0};
        float cloudTime = 0.0f;
        float jitter[2] = {0, 0};
        bool dimensionNether = false, dimensionEnd = false;
        int dimensionId = 0;
        float skyBrightnessSmooth = 1.0f;
        float screenBrightnessSetting = 1.0f;
        int blendFunc[4] = {0, 0, 0, 0};
        float terrainIconSize = 0.0f;
        float velocity[3] = {0, 0, 0};
        float playerLookVector[3] = {0, 0, -1};
        bool inSwamp = false;
        float hdrBrightness = 1.0f;
        float darknessFactor = 0.0f, darknessLightFactor = 0.0f, heavyFog = 0.0f, cloudHeight = 192.0f;
        bool isElytraFlying = false;
        float relativeEyePosition[3] = {0, 0, 0};
        float playerBodyVector[3] = {0, 0, -1};
    };

    // Per-draw values that change between submissions of the same program.
    struct DrawState {
        float modelView[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // gl_ModelViewMatrix
        float normalMatrix[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        float textureMatrix0[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        float textureMatrix1[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        float projection[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // gl_ProjectionMatrix
        float entityColor[4] = {0, 0, 0, 0};
        float fogColor[4] = {1, 1, 1, 1};
        float fogDensity = 0.0f, fogStart = 0.0f, fogEnd = 128.0f;
        int entityId = 0, blockEntityId = 0, currentRenderedItemId = 0;
        int renderStage = 0;
        int viewHeight = 720;
        bool overrideEntity = false;
    };

    class UniformTable;

    // Compiled `uniform.<type>.<name> = <expression>` / `variable.<type>.<name> = <expression>` entries.
    class CustomUniforms {
    public:
        struct Def {
            std::string type;   // float, int, bool, vec2, vec3, vec4
            std::string name;
            std::string source;
            bool variable = false;
        };
        CustomUniforms();
        ~CustomUniforms();
        // Parses the property entries; returns the problems found (unparsable expressions are skipped).
        std::vector<std::string> Load(const std::vector<std::pair<std::string, std::string>>& properties);
        bool Has(const std::string& name) const;
        // Evaluates every uniform for this frame (smoothing state advances with frameTime).
        void Evaluate(const FrameState& state, const UniformTable& table);
        // Writes the natural components of a uniform ("float" 1, "vec3" 3 ...). False when unknown.
        bool Get(const std::string& name, float* out, int& components) const;
        std::string TypeOf(const std::string& name) const;
        size_t Count() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    // Maps shader uniform names to engine values.
    class UniformTable {
    public:
        UniformTable();
        ~UniformTable();

        struct Context {
            const FrameState* frame = nullptr;
            const DrawState* draw = nullptr;
        };

        // Writes the natural components of the uniform. Returns false for unknown names.
        bool Evaluate(const std::string& name, const Context& ctx, float* out, int& components) const;
        bool Known(const std::string& name) const;
        // Scalar view of any known uniform's first component (used by the expression engine).
        bool Scalar(const std::string& name, const Context& ctx, double& out) const;

        void SetCustom(const CustomUniforms* custom) { this->custom = custom; }

    private:
        using Fn = std::function<void(const Context&, float*)>;
        struct Entry {
            Fn fn;
            int components = 1;
        };
        void Add(const char* name, int components, Fn fn);
        std::unordered_map<std::string, Entry> entries;
        const CustomUniforms* custom = nullptr;
    };

    // Packs a uniform's natural component values into the vec4/mat4 register layout the transformer declares.
    void PackUniform(const UniformDecl& decl, const float* natural, float* registers);
    int NaturalComponents(const UniformDecl& decl);
}
