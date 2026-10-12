#pragma once
#include "client/renderer/block/BlockModels.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin {

    // Block geometry (.geo.json) and block definitions read dynamically from Bedrock resource/behavior packs.
    // Takes priority over the baked Java models.
    class BlockGeometry {
    public:
        using FetchFn = std::function<int(const std::string& relPath, const std::string& root)>;

        // packRoot: folder containing models/ and textures/terrain_texture.json; blocksDir: folder of
        // block definition .json files (behavior pack).
        void ScanPack(const std::string& packRoot, const std::string& blocksDir, const FetchFn& fetch);
        bool Empty() const { return blocks.empty(); }
        size_t Count() const { return blocks.size(); }

        // Entry for a runtime block id, or nullptr when no pack defines geometry for it.
        const ModelEntry* Find(uint16_t localId);

    private:
        struct Cube {
            float origin[3] = {0, 0, 0}, size[3] = {0, 0, 0};
            float pivot[3] = {0, 0, 0}, rot[3] = {0, 0, 0};
            float inflate = 0;
            bool mirror = false, boxUv = false;
            float uvBox[2] = {0, 0};
            struct Face {
                bool has = false;
                float uv[2] = {0, 0}, size[2] = {0, 0};
                std::string mat;
            } faces[6];
            std::string mat; // cube-level material_instance for box uv
        };
        struct Bone {
            std::string name, parent;
            float pivot[3] = {0, 0, 0}, rot[3] = {0, 0, 0};
            std::vector<Cube> cubes;
        };
        struct Geometry {
            float tw = 16, th = 16;
            std::vector<Bone> bones;
        };
        struct Components {
            bool hasGeometry = false;
            std::string geometry;
            bool hasMaterials = false;
            std::unordered_map<std::string, int> tiles; // material instance -> atlas tile
            std::unordered_map<std::string, bool> dimming;
            bool hasTransform = false;
            float rot[3] = {0, 0, 0}, trans[3] = {0, 0, 0}, scale[3] = {1, 1, 1};
        };
        struct Block {
            Components base;
            std::vector<std::pair<std::string, Components>> perms;
        };

        void LoadGeometries(const std::string& dir);
        std::vector<ModelQuad> Bake(const Geometry& g, const Components& c) const;

        std::unordered_map<std::string, Geometry> geometries;
        std::unordered_map<std::string, Block> blocks;

        std::shared_mutex mutex;
        std::unordered_map<uint16_t, const ModelEntry*> byId;
        std::deque<ModelEntry> storage;
    };

}
