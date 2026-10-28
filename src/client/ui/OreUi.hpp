#pragma once
#include <string>

// Perlin's own drawing of Bedrock's OreUI look: flat faces with a 1-texel dark outline, a lighter top-left edge and,
// on elevated controls, a 2-texel lip that disappears while pressed. One texel is one GUI pixel. Colours are the
// values the game's OreUI uses; all geometry is plain RML boxes, so no game artwork is needed.
namespace perlin::ui::ore {
    enum class Kind { Primary, Neutral, Secondary, Destructive };

    // Stylesheet for everything below; put it inside the document's <style>.
    const std::string& Styles();

    // An elevated OreUI button. `id` is what OverlayHover() reports; an empty id makes it inert.
    // `big` uses the heading size (16 px) for the label, as the game does for its primary actions.
    std::string Button(const std::string& id, int x, int y, int w, int h, const std::string& label, Kind kind,
                       bool big = false, bool disabled = false);
    // A square elevated button holding a 16x16 icon (any src mcimg understands).
    std::string IconButton(const std::string& id, int x, int y, int size, const std::string& iconSrc, Kind kind);
    // A dark rounded-corner OreUI card (the panels behind character previews and dialogs).
    std::string Card(int x, int y, int w, int h, int z = 2);
    // Single-line text in the OreUI body style; `muted` uses the secondary text colour.
    std::string Label(int x, int y, int w, const std::string& text, const char* align = "left", bool muted = false,
                      bool big = false);
}
