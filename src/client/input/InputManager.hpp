#pragma once
#include "client/Options.hpp"
#include "client/input/Input.hpp"
#include "client/input/KeyMapping.hpp"

#include <array>
#include <bitset>
#include <chrono>
#include <optional>
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
        uint32_t locks = 0;
        std::array<std::chrono::steady_clock::time_point, kActionCount> holds{};
    };
}
