#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <array>
#include <unordered_map>
#include <vector>

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
        static constexpr int kTilePx = 16;
        static constexpr int kAtlasTiles = 128;

        bool EnsureLoaded(const std::string& perlinPackDir, const std::string& vanillaRpRoot);
        bool Ready() const { return ready; }
        bool Complete() const { return complete; }

        const BlockAppearance& Look(int32_t localId) const;

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
        void PollAnimation(uint64_t step, const std::function<void(int tile, const uint8_t* rgba16)>& emit);

    private:
        int FetchTile(const std::string& pngRelPath, const std::string& vanillaRpRoot);

        bool ready = false;
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
            std::vector<uint8_t> pixels;
            int w = 0, h = 0;
        };
        std::unordered_map<int, AnimSrc> animByTile;
        std::vector<uint8_t> animScratch = std::vector<uint8_t>(16 * 16 * 4, 0);
        std::unordered_map<std::string, std::array<float, 3>> baseTints;
        mutable std::unordered_map<int32_t, bool> unknownLogged;
        mutable std::vector<const BlockAppearance*> localLook;
        mutable std::mutex lookMutex;
    };

}
