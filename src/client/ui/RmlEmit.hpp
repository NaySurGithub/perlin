#pragma once
#include "client/ui/UiLayout.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin::ui {
    struct ImageStyle {
        float u = 0, v = 0;
        float uw = -1, vh = -1;
        bool hasNine = false;
        float nine[4] = {0, 0, 0, 0};
        int tiled = 0;
        float tileScale = 1.0f;
        std::string clipDirection;
        float clipRatio = 1.0f;
        float r = 1, g = 1, b = 1, alpha = 1;
        bool keepRatio = false;
    };

    struct TextStyle {
        float scale = 1.0f;
        float r = 1, g = 1, b = 1, a = 1;
        bool shadow = false;
        bool outline = false;
        std::string align = "left";
        float linePadding = 0;
    };

    class RmlWriter {
    public:
        explicit RmlWriter(bool comments) : comments(comments) {}

        void Image(float ax, float ay, float w, float h, int z, const std::string& texture, const ImageStyle& style);
        void Item(float ax, float ay, float w, float h, int z, const std::string& itemKey, float alpha = 1.0f,
                  bool glint = false);
        void Rect(float ax, float ay, float w, float h, int z, float r, float g, float b, float a);
        void Text(float ax, float ay, float w, float h, int z, const std::vector<std::string>& lines, const TextStyle& style);
        void Comment(const std::string& text);

        const std::string& Body() const { return body; }
        size_t Elements() const { return count; }

    private:
        std::string body;
        bool comments;
        size_t count = 0;
    };

    using CustomRenderer = std::function<void(const LNode&, RmlWriter&)>;

    class RmlEmitter {
    public:
        void Register(const std::string& name, CustomRenderer renderer) { renderers[name] = std::move(renderer); }

        std::string Emit(const LNode& root, float guiW, float guiH, const std::string& extraRcss, bool comments);
        const std::vector<std::string>& UnknownRenderers() const { return unknown; }

    private:
        void Visit(const LNode& n, RmlWriter& out);
        void EmitImage(const LNode& n, RmlWriter& out);
        void EmitLabel(const LNode& n, RmlWriter& out);

        std::unordered_map<std::string, CustomRenderer> renderers;
        std::vector<std::string> unknown;
    };

    std::string EscapeRml(const std::string& text);
}
