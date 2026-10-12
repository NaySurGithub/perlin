#pragma once
#include "client/input/Keys.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace perlin {
    enum class Action : uint8_t {
        Forward,
        Back,
        Left,
        Right,
        Jump,
        Sneak,
        Look,
        CyclePerspective,
        Screenshot,
        Sprint,
        Zoom,
        Count
    };

    constexpr size_t kActionCount = static_cast<size_t>(Action::Count);

    struct Binding {
        enum class Device : uint8_t { None, Keyboard, Mouse };
        Device device = Device::None;
        int code = 0;

        static Binding KeyboardKey(int code) { return {Device::Keyboard, code}; }
        static Binding MouseButton(int code) { return {Device::Mouse, code}; }

        bool operator==(const Binding& o) const { return device == o.device && code == o.code; }
        std::string Serialize() const;
        static Binding Parse(const std::string& text);
    };

    using KeyBindings = std::array<Binding, kActionCount>;

    const char* ActionId(Action action);
    KeyBindings DefaultBindings();
}
