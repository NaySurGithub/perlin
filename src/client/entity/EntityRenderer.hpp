#pragma once

#include "client/entity/EntityAssets.hpp"
#include "client/entity/EntityTracker.hpp"
#include "client/renderer/RenderContext.hpp"

#include <array>
#include <functional>
#include <memory>
#include <unordered_map>

namespace perlin {
    class HandRenderer;
}

namespace perlin::entity {
    class EntityRenderer {
    public:
        EntityRenderer();
        ~EntityRenderer();
        void Initialize(RenderContext& renderer);

        std::pair<float, float> HitBox(const Entity& e) const;
        void SetItemRenderer(HandRenderer* items) { itemRenderer = items; }
        void Shutdown();
        void ClearCaches();

        struct FrameIn {
            double time = 0;
            float dt = 0.016f;
            glm::dvec3 camera{0.0};
            std::function<glm::vec3(const glm::dvec3&)> light;
            bool firstPerson = true;
            float maxDistance = 96.0f;
            float cameraYaw = 0, cameraPitch = 0;
            double worldTicks = 0;
            const Frustum* frustum = nullptr;
            bool outline = false;
            bool hitboxes = false;
            std::vector<std::array<int, 4>> cracks;
            std::function<double(double, double, double)> groundY;
            bool chunkBorders = false;
            glm::dvec3 playerPos{0.0};
            float pixelScale = 0;
            float lineWidthPx = 2.5f;
            glm::dvec3 outlineLo{0.0}, outlineHi{0.0};
            std::vector<std::pair<glm::dvec3, glm::dvec3>> outlineShape;
        };
        void Render(const std::vector<Entity>& entities, const FrameIn& in);

        size_t Drawn() const { return drawn; }
        size_t Considered() const { return considered; }
        size_t Unrenderable() const { return unrenderable; }
        size_t Culled() const { return culled; }

    private:
        HandRenderer* itemRenderer = nullptr;
        struct View;
        struct Layer {
            std::vector<attachable::Quad> quads;
            std::string texture;
            bool translucent = false;
            float tint[4] = {1, 1, 1, 1};
            MaterialInfo material;
        };
        bool Build(const Entity& e, View& v, const FrameIn& in, std::vector<Layer>& layers, float& scale);
        void RunAnimations(const Entity& e, View& v, const ClientEntityDef& def, attachable::Pose& pose, const FrameIn& in,
                           const attachable::Geometry* bindGeometry);
        void Draw(const Entity& e, const View& v, const std::vector<Layer>& layers, float scale, const FrameIn& in);
        uint32_t TextureFor(const Entity& e, const std::string& key);
        void Track(const Entity& e, View& v, const FrameIn& in);
        void DrawShadow(const glm::dvec3& pos, float radius, float strength, const FrameIn& in);
        uint32_t shadowTexture = 0;

        RenderContext* renderer = nullptr;
        EntityAssets assets;
        std::unordered_map<uint64_t, std::unique_ptr<View>> views;
        std::unordered_map<std::string, uint32_t> textures;
        std::unordered_map<uint64_t, uint32_t> skinTextures, capeTextures;
        std::unordered_map<uint64_t, std::unique_ptr<attachable::GeometryLibrary>> skinGeometry;
        std::vector<uint32_t> pendingMeshes;
        size_t drawn = 0, considered = 0, unrenderable = 0, culled = 0;
        struct Line {
            glm::dvec3 a, b;
            uint32_t colour;
            float width = 1.0f;
            uint32_t colourEnd = 0;
        };
        std::vector<Line> lines;
        void AddBox(const glm::dvec3& lo, const glm::dvec3& hi, uint32_t colour, float width = 1.0f);
        void AddShapeOutline(const std::vector<std::pair<glm::dvec3, glm::dvec3>>& boxes, uint32_t colour);
        void DrawCracks(const FrameIn& in);
        size_t outlineLineStart = 0;
        void AddHitbox(const Entity& e, const View& v);
        void DrawLines(const FrameIn& in);
        std::string loadedRoot;
        std::unordered_map<std::string, bool> warned;
    };
}
