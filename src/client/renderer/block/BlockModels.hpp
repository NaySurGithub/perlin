#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace perlin {

    // Block models baked offline from Java's blockstates/models (tools/modelgen) and keyed by Bedrock
    // network block hash. Quads are in block space (0..1), UVs in 0..16 texel space.
    struct ModelQuad {
        uint8_t dir;      // 0 down 1 up 2 north 3 south 4 west 5 east (lighting/AO face)
        uint8_t shadeDir; // direction used for directional shading
        int8_t cull;      // -1 or direction index
        int8_t tint;      // tintindex or -1
        uint8_t shade;    // directional shading enabled
        uint8_t light;    // light emission
        uint16_t tex;     // index into Textures(); 0xFFFF = none
        float pos[4][3];
        float uv[4][2];
        int tile = 0; // direct atlas tile (resource-pack geometry); 0 = use tex
    };

    struct ModelEntry {
        uint8_t offset = 0; // 0 none, 1 XZ, 2 XYZ, 3 dripstone
        bool cube = false;  // full opaque cube: keeps the legacy occlusion/light behaviour
        struct Choice {
            uint32_t variant;
            uint8_t weight;
        };
        std::vector<std::vector<Choice>> groups;
        bool direct = false; // quads below are used instead of groups (resource-pack geometry)
        std::vector<ModelQuad> quads;
    };

    class BlockModels {
    public:
        bool Load(const std::string& path);
        bool LoadFromMemory(const std::string& bytes);
        // Replaces / adds a model for a network hash (used by resource-pack geometry).
        bool Loaded() const { return loaded; }
        const std::vector<std::string>& Textures() const { return textures; }
        const ModelEntry* Find(int32_t hash) const {
            auto it = byHash.find(hash);
            return it == byHash.end() ? nullptr : &entries[it->second];
        }
        const std::vector<ModelQuad>& Variant(uint32_t id) const { return variants[id]; }
        size_t BlockCount() const { return byHash.size(); }

        // Atlas tile per texture (filled by BlockLibrary); 0 = missing.
        std::vector<int> tiles;

    private:
        bool loaded = false;
        std::vector<std::string> textures;
        std::vector<std::vector<ModelQuad>> variants;
        std::vector<ModelEntry> entries;
        std::unordered_map<int32_t, uint32_t> byHash;
    };

}
