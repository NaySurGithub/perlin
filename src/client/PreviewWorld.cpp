#include "client/PreviewWorld.hpp"
#include "util/Paths.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <string>

#include "util/Resources.hpp"

namespace perlin {
    namespace {

        class Ids {
        public:
            Ids() {
                resources::Read("block_states.json", text);
            }
            uint16_t Get(const std::string& name, const std::string& stateFragment = "") {
                const std::string key = name + "|" + stateFragment;
                if (const auto it = cache.find(key); it != cache.end())
                    return it->second;
                uint16_t id = 0;
                size_t at = 0;
                const std::string needle = ",\"minecraft:" + name + "\",";
                while ((at = text.find(needle, at)) != std::string::npos) {
                    const size_t open = text.rfind('[', at);
                    const size_t close = text.find("}]", at);
                    const std::string entry = text.substr(open, close - open + 2);
                    at += needle.size();
                    if (!stateFragment.empty() && entry.find(stateFragment) == std::string::npos)
                        continue;
                    id = BlockPalette::Instance().Intern(std::stoi(text.substr(open + 1)));
                    break;
                }
                cache[key] = id;
                return id;
            }

        private:
            std::string text;
            std::map<std::string, uint16_t> cache;
        };

        float Hash(int x, int z) {
            uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u;
            h = (h ^ (h >> 13)) * 1274126177u;
            return static_cast<float>((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
        }

        float Noise(float x, float z) {
            const int xi = static_cast<int>(std::floor(x)), zi = static_cast<int>(std::floor(z));
            const float fx = x - xi, fz = z - zi;
            const float ux = fx * fx * (3 - 2 * fx), uz = fz * fz * (3 - 2 * fz);
            const float a = Hash(xi, zi), b = Hash(xi + 1, zi), c = Hash(xi, zi + 1), d = Hash(xi + 1, zi + 1);
            return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uz;
        }

        int Height(int x, int z) {
            const float n = Noise(x * 0.018f, z * 0.018f) * 0.62f + Noise(x * 0.05f, z * 0.05f) * 0.28f +
                            Noise(x * 0.13f, z * 0.13f) * 0.10f;
            return 52 + static_cast<int>(n * 34.0f);
        }
    }

    bool BuildPreviewWorld(Level& level, int radius) {
        if (!BlockPalette::Instance().NamesLoaded())
            return false;
        Ids ids;
        const uint16_t stone = ids.Get("stone"), dirt = ids.Get("dirt"), grass = ids.Get("grass_block"),
                       sand = ids.Get("sand"), water = ids.Get("water", "\"liquid_depth\":[\"i\",0]"),
                       log = ids.Get("oak_log", "\"pillar_axis\":[\"s\",\"y\"]"), leaves = ids.Get("oak_leaves"),
                       planks = ids.Get("oak_planks"), glow = ids.Get("glowstone"), bedrock = ids.Get("bedrock"),
                       cobble = ids.Get("cobblestone"), snow = ids.Get("snow"), glass = ids.Get("glass"),
                       torch = ids.Get("torch", "\"torch_facing_direction\":[\"s\",\"top\"]"),
                       lava = ids.Get("lava", "\"liquid_depth\":[\"i\",0]");
        constexpr int kSea = 62;
        auto set = [&](int x, int y, int z, uint16_t id) { level.SetBlock(x, y, z, id); };

        for (int cx = -radius; cx < radius; cx++)
            for (int cz = -radius; cz < radius; cz++)
                level.EnsureChunk(cx, cz);

        const int span = radius * 16;
        for (int x = -span; x < span; x++)
            for (int z = -span; z < span; z++) {
                const int h = Height(x, z);
                set(x, -64, z, bedrock);
                for (int y = -63; y <= h; y++) {
                    uint16_t id = stone;
                    if (y == h)
                        id = h <= kSea + 1 ? sand : grass;
                    else if (y >= h - 3)
                        id = h <= kSea + 1 ? sand : dirt;
                    set(x, y, z, id);
                }
                for (int y = h + 1; y <= kSea; y++)
                    set(x, y, z, water);
            }

        for (int x = 8; x <= 30; x++)
            for (int z = -6; z <= 6; z++)
                for (int y = 30; y <= 36; y++)
                    if ((z * z) / 36.0f + ((y - 33) * (y - 33)) / 12.0f < 1.0f)
                        set(x, y, z, 0);
        for (int x = 12; x <= 28; x += 4)
            set(x, 31, 4, torch);
        for (int x = 20; x <= 24; x++)
            for (int z = -3; z <= 0; z++)
                set(x, 29, z, lava);

        for (int y = 30; y <= 33; y++)
            set(20, y, 0, stone);
        set(19, 32, 0, ids.Get("torch", "\"torch_facing_direction\":[\"s\",\"west\"]"));
        set(21, 32, 0, ids.Get("torch", "\"torch_facing_direction\":[\"s\",\"east\"]"));
        set(20, 32, -1, ids.Get("torch", "\"torch_facing_direction\":[\"s\",\"north\"]"));
        set(20, 32, 1, ids.Get("torch", "\"torch_facing_direction\":[\"s\",\"south\"]"));

        for (int x = -span + 4; x < span - 4; x += 3)
            for (int z = -span + 4; z < span - 4; z += 3) {
                const int tx = x + static_cast<int>(Hash(x, z) * 3), tz = z + static_cast<int>(Hash(z, x) * 3);
                const int h = Height(tx, tz);
                if (h <= kSea + 2 || Hash(tx * 7, tz * 13) > 0.16f || (std::abs(tx) < 12 && std::abs(tz) < 12))
                    continue;
                const int trunk = 4 + static_cast<int>(Hash(tx, tz + 5) * 3);
                for (int y = h + 1; y <= h + trunk; y++)
                    set(tx, y, tz, log);
                for (int dy = trunk - 2; dy <= trunk + 1; dy++) {
                    const int r = dy > trunk - 1 ? 1 : 2;
                    for (int dx = -r; dx <= r; dx++)
                        for (int dz = -r; dz <= r; dz++)
                            if ((std::abs(dx) + std::abs(dz) < r * 2 || Hash(tx + dx, tz + dz) > 0.5f) &&
                                level.GetBlock(tx + dx, h + dy, tz + dz) == 0)
                                set(tx + dx, h + dy, tz + dz, leaves);
                }
            }

        const int padY = Height(0, 0);
        for (int x = -7; x <= 7; x++)
            for (int z = -7; z <= 7; z++) {
                for (int y = padY + 1; y <= padY + 8; y++)
                    set(x, y, z, 0);
                set(x, padY, z, grass);
            }
        for (int x = -4; x <= 4; x++)
            for (int z = -4; z <= 4; z++) {
                set(x, padY, z, planks);
                const bool wall = std::abs(x) == 4 || std::abs(z) == 4;
                for (int y = padY + 1; y <= padY + 3; y++)
                    if (wall)
                        set(x, y, z, (x == 4 && z == 0 && y <= padY + 2) ? 0 : (y == padY + 2 && z == -4 && std::abs(x) < 2) ? glass : cobble);
                set(x, padY + 4, z, planks);
            }
        set(0, padY + 3, 0, glow);
        set(-3, padY + 1, -3, torch);
        return true;
    }
}
