#pragma once
#include "client/input/KeyMapping.hpp"

#include <map>
#include <string>

namespace perlin {
    // Where a touch button sits, as fractions of the screen (-1 = the built-in spot), and its size multiplier.
    struct TouchPlace {
        float x = -1.0f;
        float y = -1.0f;
        float scale = 1.0f;
        bool operator==(const TouchPlace&) const = default;
    };

    // What the touch overlay needs to know about the game each frame: which icons to show (swimming and
    // flying swap the jump and sneak buttons for ascend/descend, riding turns sneak into dismount), where the
    // hotbar is, the player's key bindings so the buttons press whatever is bound, and the saved layout.
    struct TouchState {
        bool flying = false;
        bool inWater = false;
        bool riding = false;
        bool editing = false; // the layout editor is open: buttons are dragged, not pressed
        int hudScale = 0;
        float scale = 1.0f;
        float opacity = 0.85f;
        float sensitivity = 1.0f;
        std::map<std::string, TouchPlace> layout;
        KeyBindings bindings = DefaultBindings();
    };
}
