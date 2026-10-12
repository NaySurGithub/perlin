#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <array>
#include <unordered_map>
#include <vector>
#include "client/renderer/block/BlockGeometry.hpp"
#include "client/renderer/block/BlockModels.hpp"

namespace perlin {

    struct BlockAppearance {
        int top = 0;
        int side = 0;
        int bottom = 0;
        std::vector<int> varTop, varSide, varBottom;
        float overlayFrac = 0;
        int overlayTile = 0;
        float tintR = 1, tintG = 1, tintB = 1;
        bool tintTop = false;
        bool tintSide = false;
        bool tintBottom = false;
        std::string carriedTex;
        bool useCarriedColor = false;
        bool hasCarriedRGB = false;
        float carriedR = 1, carriedG = 1, carriedB = 1;
        enum class Shape { Cube, Cross, Flat, Wall };
        Shape shape = Shape::Cube;
        bool cutout = false;
        std::string texTop, texSide, texBottom;
    };

    class BlockLibrary {
    public:
        static int TilePx() { return tilePxValue; }
        static constexpr int kAtlasTiles = 128;

        // Picks the tile size from the active TexturePack (a power of two from its block textures; 16 for vanilla).
        // Every texture is resampled to that one square size.
        void SetTexturePack(bool active);
        bool EnsureLoaded(const std::string& perlinPackDir, const std::string& vanillaRpRoot);
        bool Ready() const { return ready; }
        void SetFancy(bool value) { fancy = value; }
        bool Complete() const { return complete; }

        const BlockAppearance& Look(int32_t localId) const;
        const BlockModels& Models() const { return models; }
        BlockGeometry& Geometry() const { return geometry; }

        const std::vector<uint8_t>& AtlasPixels() const { return atlas; }
        int TilesLoaded() const { return nextTile; }
        static void TileUV(int tile, float& u0, float& v0, float& u1, float& v1);

        struct TileAlpha {
            bool holes = false;
            bool translucent = false;
            bool centerHollow = false;
            float saturation = 0;
            float frame = 0;
            float holeFrac = 0;
        };
        TileAlpha AlphaOf(int tile) const;
        float SaturationOf(int tile) const;
        std::string TilePathOf(int tile) const;
        // Atlas layer of the first loaded texture whose path contains `part` (-1 when none is loaded).
        int TileContaining(const std::string& part) const;
        void PollAnimation(uint64_t step, const std::function<void(int tile, const uint8_t* rgba)>& emit);

    private:
        int FetchTile(const std::string& pngRelPath, const std::string& vanillaRpRoot);

        BlockModels models;
        mutable BlockGeometry geometry;
        bool modelsTried = false;
        bool geometryTried = false;
        bool ready = false;
        bool fancy = true;
        bool complete = false;
        int fetchFails = 0;
        int lastFileMisses = -1;
        int stallStreak = 0;
        int nextTile = 1;
        std::unordered_map<std::string, BlockAppearance> appearances;
        std::vector<uint8_t> atlas;
        std::unordered_map<std::string, int> tileByPng;
        std::unordered_map<int, TileAlpha> alphaByTile;
        std::unordered_map<int, std::string> pathByTile;
        struct AnimSrc {
            std::vector<uint8_t> pixels; // frames stacked vertically, each already resampled to tilePx
            int frames = 0;
        };
        std::unordered_map<int, AnimSrc> animByTile;
        int NewTile();
        inline static int tilePxValue = 16;
        std::unordered_map<std::string, std::array<float, 3>> baseTints;
        mutable std::unordered_map<int32_t, bool> unknownLogged;
        mutable std::vector<const BlockAppearance*> localLook;
        mutable std::mutex lookMutex;
    };

}
