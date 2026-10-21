#include "client/renderer/WeatherRenderer.hpp"

#include <algorithm>
#include <cmath>

#include "client/renderer/RenderContext.hpp"
#include "client/renderer/block/BiomeTints.hpp"
#include "client/ui/UiAssets.hpp"
#include "world/level/Level.hpp"
#include "world/level/block/BlockPalette.hpp"

namespace perlin {
    namespace {
        int64_t Key(int x, int z) {
            return (static_cast<int64_t>(x) << 32) ^ static_cast<int64_t>(static_cast<uint32_t>(z));
        }

        uint32_t Hash(int x, int z) {
            uint32_t h = static_cast<uint32_t>(x * x * 3121 + x * 45238971) ^ static_cast<uint32_t>(z * z * 418711 + z * 13761);
            h ^= h >> 15;
            h *= 0x2c1b3c6du;
            h ^= h >> 12;
            return h;
        }

        uint32_t Pack(float brightness, float alpha) {
            const auto q = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
            return (q(alpha) << 24) | (q(brightness) << 16) | (q(brightness) << 8) | q(brightness);
        }
    }

    void WeatherRenderer::Reset() {
        columns.clear();
        blocking.clear();
    }

    bool WeatherRenderer::Blocks(Level&, uint16_t id) {
        if (id == 0)
            return false;
        const auto found = blocking.find(id);
        if (found != blocking.end())
            return found->second;
        const std::string name = BlockPalette::Instance().Name(id);
        static const char* const kThrough[] = {"air",  "short_grass", "tallgrass", "tall_grass", "fern",   "flower", "poppy", "dandelion",
                                               "torch", "sapling",     "vine",      "sugar_cane", "wheat",  "rail",   "button", "lever",
                                               "tripwire", "web",      "fire",      "mushroom",   "sweet_berry"};
        bool blocks = true;
        for (const char* word : kThrough)
            if (name.find(word) != std::string::npos) {
                blocks = false;
                break;
            }
        blocking[id] = blocks;
        return blocks;
    }

    void WeatherRenderer::LoadTextures(RenderContext& renderer) {
        texturesTried = true;
        Texture t;
        if (!ui::UiAssets::Instance().Image("textures/environment/weather", t) || t.width < 32 || t.height < 20)
            return;
        const auto crop = [&](int firstRow, int rows) {
            std::vector<uint8_t> px(static_cast<size_t>(t.width) * 16 * 4, 0);
            for (int y = 0; y < rows && y < 16; y++)
                std::copy_n(t.pixels.begin() + (static_cast<size_t>(firstRow + y) * t.width) * 4, static_cast<size_t>(t.width) * 4,
                            px.begin() + static_cast<size_t>(y) * t.width * 4);
            return px;
        };
        const std::vector<uint8_t> rainPx = crop(4, 16);
        const std::vector<uint8_t> snowPx = crop(0, 4);
        rainTexture = renderer.UiCreateTexture(t.width, 16, rainPx.data(), false, true);
        snowTexture = renderer.UiCreateTexture(t.width, 16, snowPx.data(), false, true);
    }

    void WeatherRenderer::Draw(const In& in) {
        if (!in.enabled || !in.renderer || !in.level || in.rain < 0.01f)
            return;
        if (!texturesTried)
            LoadTextures(*in.renderer);
        if (!rainTexture || !snowTexture)
            return;
        Level& level = *in.level;
        const int radius = std::clamp(in.radius, 3, 12);
        const int cx = static_cast<int>(std::floor(in.camera.x)), cz = static_cast<int>(std::floor(in.camera.z));
        const int camY = static_cast<int>(std::floor(in.camera.y));

        int budget = 48;
        if (columns.size() > static_cast<size_t>((2 * radius + 1) * (2 * radius + 1) * 3)) {
            for (auto it = columns.begin(); it != columns.end();) {
                const int x = static_cast<int>(it->first >> 32), z = static_cast<int>(static_cast<int32_t>(it->first & 0xffffffff));
                if (std::abs(x - cx) > radius + 4 || std::abs(z - cz) > radius + 4)
                    it = columns.erase(it);
                else
                    ++it;
            }
        }
        const auto refresh = [&](int x, int z, Column& c) {
            bool loaded = false;
            level.GetBlock(x, camY, z, loaded);
            if (!loaded) {
                c.kind = 0;
                c.stamp = in.seconds - 0.8;
                return;
            }
            const int highest = std::min(camY + 40, 318);
            const int lowest = camY - 14;
            int top = lowest;
            for (int y = highest; y >= lowest; y--) {
                if (Blocks(level, level.GetBlock(x, y, z)) || level.GetLiquid(x, y, z) != 0) {
                    top = y + 1;
                    break;
                }
            }
            c.top = top;
            c.kind = 1;
            if (in.tints && in.tints->Ready()) {
                float temperature = 0.5f, downfall = 0.5f;
                const std::string name = in.tints->NameForId(level.GetBiome(x, std::clamp(top - 1, -64, 318), z));
                if (in.tints->Climate(name, temperature, downfall)) {
                    if (downfall <= 0.0f)
                        c.kind = 0;
                    else if (temperature < 0.15f)
                        c.kind = 2;
                }
            }
            c.stamp = in.seconds;
        };

        std::vector<UiVertex> rainV, snowV;
        std::vector<uint32_t> rainI, snowI;
        const double t = in.seconds;
        for (int dz = -radius; dz <= radius; dz++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx * dx + dz * dz > radius * radius)
                    continue;
                const int x = cx + dx, z = cz + dz;
                Column& c = columns[Key(x, z)];
                if (in.seconds - c.stamp > 1.2 && budget > 0) {
                    budget--;
                    refresh(x, z, c);
                }
                if (c.kind == 0)
                    continue;
                const int yTop = camY + radius, yBottom = std::max(c.top, camY - radius);
                if (yBottom >= yTop)
                    continue;
                const double px = x + 0.5 - in.camera.x, pz = z + 0.5 - in.camera.z;
                const double dist = std::sqrt(px * px + pz * pz);
                const float fade = static_cast<float>(1.0 - dist * dist / static_cast<double>((radius + 1) * (radius + 1)));
                const float alpha = std::clamp((fade * 0.5f + 0.5f) * in.rain * (c.kind == 2 ? 0.85f : 0.7f), 0.0f, 1.0f);
                if (alpha < 0.02f)
                    continue;
                const double len = std::max(dist, 0.001);
                const float ax = static_cast<float>(-pz / len * 0.5), az = static_cast<float>(px / len * 0.5);
                const uint32_t h = Hash(x, z);
                const float offset = static_cast<float>(h & 255) / 255.0f;
                const float bright = 0.6f + 0.4f * std::clamp(in.light, 0.0f, 1.0f);
                const uint32_t color = Pack(c.kind == 2 ? std::min(1.0f, bright + 0.1f) : bright * 0.92f, alpha);
                const float x0 = static_cast<float>(x + 0.5) - ax, x1 = static_cast<float>(x + 0.5) + ax;
                const float z0 = static_cast<float>(z + 0.5) - az, z1 = static_cast<float>(z + 0.5) + az;
                const float y0 = static_cast<float>(yBottom), y1 = static_cast<float>(yTop);
                float u0, u1, v0, v1;
                if (c.kind == 1) {
                    const float fall = static_cast<float>(std::fmod(t * 12.0 + (h >> 8 & 31), 2.0));
                    u0 = offset;
                    u1 = offset + 1.0f;
                    v0 = -(y1 + fall) * 0.5f;
                    v1 = -(y0 + fall) * 0.5f;
                } else {
                    const float fall = static_cast<float>(std::fmod(t * 1.4 + (h >> 8 & 31) * 0.1, 1.0));
                    const float sway = static_cast<float>(std::sin(t * 0.8 + (h & 63)) * 0.25);
                    u0 = offset * 2.0f + sway;
                    u1 = u0 + 0.5f;
                    v0 = -(y1 + fall);
                    v1 = -(y0 + fall);
                }
                std::vector<UiVertex>& vs = c.kind == 1 ? rainV : snowV;
                std::vector<uint32_t>& is = c.kind == 1 ? rainI : snowI;
                const uint32_t base = static_cast<uint32_t>(vs.size());
                vs.push_back({x0, y0, z0, color, u0, v1});
                vs.push_back({x1, y0, z1, color, u1, v1});
                vs.push_back({x1, y1, z1, color, u1, v0});
                vs.push_back({x0, y1, z0, color, u0, v0});
                for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
                    is.push_back(base + k);
            }
        }
        const auto submit = [&](const std::vector<UiVertex>& vs, const std::vector<uint32_t>& is, uint32_t texture) {
            if (vs.empty())
                return;
            const uint32_t mesh = in.renderer->UiCreateFrameMesh(vs.data(), static_cast<uint32_t>(vs.size()), is.data(),
                                                                 static_cast<uint32_t>(is.size()));
            if (!mesh)
                return;
            UiDraw draw;
            draw.mesh = mesh;
            draw.texture = texture;
            draw.premultiplied = false;
            draw.alphaCutoff = 0.004f;
            draw.blend = 1;
            draw.depth = true;
            draw.depthWrite = false;
            draw.emissive = 2;
            in.renderer->UiSubmit(UiLayer::WorldLate, draw);
        };
        submit(rainV, rainI, rainTexture);
        submit(snowV, snowI, snowTexture);
    }
}
