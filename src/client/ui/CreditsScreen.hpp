#pragma once
#include <string>

namespace perlin::ui {
    // Perlin's own end credits, shown when the server sends ShowCredits (leaving the End).
    // `scroll` is how far the text has moved up from the bottom edge, in GUI pixels.
    std::string BuildCreditsScreenRml(int guiW, int guiH, int scroll);
    int CreditsHeight();
}
