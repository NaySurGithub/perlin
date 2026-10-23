#pragma once
#include "client/Options.hpp"
#include "client/input/Input.hpp"
#include "client/input/KeyMapping.hpp"

#include <array>
#include <bitset>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

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
        LookDelta Look() const { return (locks & 2u) ? LookDelta{} : look; }
        float Scroll() const { return scroll; }

        struct Typed {
            bool character = false;
            int key = 0;
            bool ctrl = false;
        };
        std::vector<Typed> TakeTyped() { return std::move(typed); }

        // Raw input for plugins that took the mouse / keyboard (Op::InputGrab). While a grab is on, the matching events
        // never reach the game; they are queued here for the plugin instead. Coordinates are framebuffer pixels.
        struct PluginInput {
            enum class Type { Move, Button, Wheel, Key, Text } type = Type::Move;
            int code = 0;
            bool down = false, repeat = false, ctrl = false, shift = false, alt = false;
            float x = 0, y = 0;
            std::string text;
        };
        void SetPluginGrab(bool mouse, bool keyboard) {
            grabMouse = mouse;
            grabKeyboard = keyboard;
        }
        std::vector<PluginInput> TakePluginInput() { return std::move(pluginEvents); }
        // Escape pressed three times within 1.5 s while a plugin holds the keyboard: the game takes everything back.
        bool TakeGrabFailsafe() {
            const bool v = failsafe;
            failsafe = false;
            return v;
        }

        void Hold(Action action, std::chrono::steady_clock::time_point until) { holds[static_cast<size_t>(action)] = until; }
        void SetBlocked(bool b) { blocked = b; }
        void SetServerLocks(uint32_t value) { locks = value; }
        bool Blocked() const { return blocked; }
        void BeginRebind(Action action) { rebinding = action; }
        bool Rebinding() const { return rebinding.has_value(); }

    private:
        void Dispatch(const Binding& pressedBinding);
        bool Matches(Action action, const Binding& binding) const;

        Input& input;
        Options& options;
        std::bitset<kActionCount> pressed;
        std::optional<Action> rebinding;
        std::vector<Typed> typed;
        LookDelta look;
        float scroll = 0;
        float lastMouseX = 0, lastMouseY = 0;
        bool haveMouse = false;
        std::chrono::steady_clock::time_point lastPoll{};
        bool blocked = false;
        bool grabMouse = false, grabKeyboard = false, failsafe = false;
        std::vector<PluginInput> pluginEvents;
        std::array<std::chrono::steady_clock::time_point, 3> escapes{};
        uint32_t locks = 0;
        std::array<std::chrono::steady_clock::time_point, kActionCount> holds{};
    };
}
