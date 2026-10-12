#pragma once
#include "client/Options.hpp"
#include "client/input/Input.hpp"
#include "client/input/KeyMapping.hpp"

#include <bitset>
#include <optional>

namespace perlin {
    struct LookDelta {
        float yaw = 0;
        float pitch = 0;
    };

    class InputManager {
    public:
        InputManager(Input& input, Options& options) : input(input), options(options) {}

        void Poll();

        bool IsDown(Action action) const;
        bool Pressed(Action action) const { return pressed.test(static_cast<size_t>(action)); }
        float Axis(Action positive, Action negative) const;
        LookDelta Look() const { return look; }
        float Scroll() const { return scroll; } // wheel notches this frame (+ = away from the user)

        void BeginRebind(Action action) { rebinding = action; }
        bool Rebinding() const { return rebinding.has_value(); }

    private:
        void Dispatch(const Binding& pressedBinding);
        bool Matches(Action action, const Binding& binding) const;

        Input& input;
        Options& options;
        std::bitset<kActionCount> pressed;
        std::optional<Action> rebinding;
        LookDelta look;
        float scroll = 0;
        float lastMouseX = 0, lastMouseY = 0;
        bool haveMouse = false;
    };
}
