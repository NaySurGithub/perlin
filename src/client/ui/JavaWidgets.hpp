#pragma once
#include <string>

namespace perlin::ui {
    // Menus build buttons as plain boxes (.btn with rim/face/shade/edge parts). This swaps those boxes for the real
    // Java button sprites (normal, highlighted, disabled; nine-sliced, so resource packs restyle them) and the slider
    // knob for the button sprite, in one place for every menu document.
    std::string WithJavaWidgets(const std::string& rml);
}
