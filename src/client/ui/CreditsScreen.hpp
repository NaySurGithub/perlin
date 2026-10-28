#pragma once
#include <string>

namespace perlin::ui {
    // The End poem, Perlin's credits and Mojang's Java credits, shown when the server sends ShowCredits (leaving the End).
    // `scroll` is how far the text has moved up from the bottom edge, in GUI pixels.
    std::string BuildCreditsScreenRml(int guiW, int guiH, int scroll, const std::string& player);
    int CreditsHeight();
}
