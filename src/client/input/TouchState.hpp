#pragma once
#include "client/input/KeyMapping.hpp"

namespace perlin {
    // What the touch overlay needs to know about the game each frame: which icons to show (swimming and
    // flying swap the jump and sneak buttons for ascend/descend, riding turns sneak into dismount), where the
    // hotbar is, and the player's key bindings so the buttons press whatever is bound.
    struct TouchState {
        bool flying = false;
        bool inWater = false;
        bool riding = false;
        int hudScale = 0;
        KeyBindings bindings = DefaultBindings();
    };
}
