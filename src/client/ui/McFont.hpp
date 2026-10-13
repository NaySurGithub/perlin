#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace perlin::ui {

    class McFont {
    public:
        struct Glyph {
            float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
            int width = 0;
            int advance = 0;
            int cell = 8;
        };

        McFont();
        static McFont& Instance();

        bool LoadSheet(const uint8_t* rgba, int width, int height);
        void UseBuiltin();

        const Glyph& Get(uint32_t codepoint) const;

        float Width(const std::string& utf8, float scale = 1.0f) const;
        static constexpr int kLineHeight = 9;
        static constexpr int kCell = 8;

        const std::vector<uint8_t>& Atlas() const { return atlas; }
        int AtlasWidth() const { return atlasWidth; }
        int AtlasHeight() const { return atlasHeight; }
        uint32_t Version() const { return version; }
        bool Builtin() const { return builtin; }

        static bool Next(const std::string& s, size_t& i, uint32_t& cp);

    private:
        void Build(const std::vector<uint8_t>& cells, int cellsAcross, int cellPx);
        std::vector<uint8_t> atlas;
        int atlasWidth = 0, atlasHeight = 0;
        std::vector<Glyph> glyphs;
        Glyph fallback;
        uint32_t version = 1;
        bool builtin = true;
    };
}
