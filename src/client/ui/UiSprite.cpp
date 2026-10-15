#include "client/ui/UiSprite.hpp"

#include "client/resources/TextureLoader.hpp"
#include "util/Resources.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <mutex>
#include <vector>

namespace perlin::ui {
    namespace {
        struct Decoded {
            bool ok = false;
            int width = 0, height = 0;
            std::vector<uint8_t> rgba;
        };

        const Decoded& Load(const std::string& resource) {
            static std::map<std::string, Decoded> cache;
            static std::mutex mutex;
            std::lock_guard lock(mutex);
            auto it = cache.find(resource);
            if (it != cache.end())
                return it->second;
            Decoded d;
            std::string bytes;
            Texture tex;
            if (resources::Read(resource, bytes) &&
                LoadTextureMemory(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), tex)) {
                d.ok = true;
                d.width = tex.width;
                d.height = tex.height;
                d.rgba.assign(tex.pixels.begin(), tex.pixels.end());
            }
            return cache.emplace(resource, std::move(d)).first->second;
        }
    }

    std::string SpriteRml(const std::string& resource, int srcX, int srcY, int width, int height, int dstX, int dstY) {
        static std::map<std::string, std::string> cache;
        static std::mutex mutex;
        const std::string key = resource + ":" + std::to_string(srcX) + "," + std::to_string(srcY) + "," + std::to_string(width) + "," +
                                std::to_string(height) + "@" + std::to_string(dstX) + "," + std::to_string(dstY);
        {
            std::lock_guard lock(mutex);
            const auto it = cache.find(key);
            if (it != cache.end())
                return it->second;
        }
        const Decoded& img = Load(resource);
        std::string out;
        if (img.ok) {
            const auto pixel = [&](int x, int y) -> uint32_t {
                const int sx = srcX + x, sy = srcY + y;
                if (sx < 0 || sy < 0 || sx >= img.width || sy >= img.height)
                    return 0;
                const uint8_t* p = &img.rgba[(static_cast<size_t>(sy) * static_cast<size_t>(img.width) + static_cast<size_t>(sx)) * 4];
                if (p[3] == 0)
                    return 0;
                return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
            };
            std::vector<uint8_t> used(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
            char color[40];
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++) {
                    const size_t at = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                    const uint32_t v = pixel(x, y);
                    if (used[at] || v == 0)
                        continue;
                    int w = 1;
                    while (x + w < width && !used[at + static_cast<size_t>(w)] && pixel(x + w, y) == v)
                        w++;
                    int h = 1;
                    for (; y + h < height; h++) {
                        bool same = true;
                        for (int k = 0; k < w && same; k++)
                            same = !used[static_cast<size_t>(y + h) * static_cast<size_t>(width) + static_cast<size_t>(x + k)] &&
                                   pixel(x + k, y + h) == v;
                        if (!same)
                            break;
                    }
                    for (int yy = 0; yy < h; yy++)
                        for (int xx = 0; xx < w; xx++)
                            used[static_cast<size_t>(y + yy) * static_cast<size_t>(width) + static_cast<size_t>(x + xx)] = 1;
                    std::snprintf(color, sizeof(color), "#%08x", v);
                    out += "<div class=\"r\" style=\"left:" + std::to_string(dstX + x) + "px;top:" + std::to_string(dstY + y) +
                           "px;width:" + std::to_string(w) + "px;height:" + std::to_string(h) + "px;background-color:" + color + ";\"></div>";
                }
        }
        std::lock_guard lock(mutex);
        return cache.emplace(key, std::move(out)).first->second;
    }
}
