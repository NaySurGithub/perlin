#pragma once
// Raw mouse and keyboard for plugins that draw their own interface (see perlin/gfx.hpp).
//
//   input::grab(true, true);                       // needs the `control` permission
//   ctx.on<event::MouseMove>([](auto& e) { ... }); // framebuffer pixels
//   ctx.on<event::MouseButton>(...); ctx.on<event::MouseWheel>(...); ctx.on<event::KeyRaw>(...); ctx.on<event::TextInput>(...);
//
// While a plugin holds the mouse the cursor is free and the game ignores the mouse; while it holds the keyboard the game
// ignores keys and text goes to the plugin as UTF-8. Pressing Escape three times quickly always gives both back.
#include "perlin/rpc.hpp"

namespace perlin::input {
    enum class Button : int32_t { Left = 0, Right = 1, Middle = 2 };

    // Virtual-key codes used by event::KeyRaw (the same numbers as perlin::Key for letters, digits, arrows and F keys).
    enum class Code : int32_t {
        Backspace = 0x08, Tab = 0x09, Enter = 0x0D, Shift = 0x10, Control = 0x11, Alt = 0x12, Escape = 0x1B, Space = 0x20,
        PageUp = 0x21, PageDown = 0x22, End = 0x23, Home = 0x24, Left = 0x25, Up = 0x26, Right = 0x27, Down = 0x28, Delete = 0x2E,
        A = 'A', C = 'C', V = 'V', X = 'X', Y = 'Y', Z = 'Z',
    };

    inline Result<void> grab(bool mouse, bool keyboard) {
        abi::Writer a;
        a.boolean(mouse).boolean(keyboard);
        return detail::call_void(Op::InputGrab, a);
    }
    inline Result<void> release() { return grab(false, false); }

    // Holds a grab for as long as the object lives.
    class Grab {
    public:
        explicit Grab(bool mouse = true, bool keyboard = true) { ok = grab(mouse, keyboard).has_value(); }
        Grab(const Grab&) = delete;
        Grab& operator=(const Grab&) = delete;
        ~Grab() {
            if (ok)
                (void)release();
        }
        explicit operator bool() const { return ok; }

    private:
        bool ok = false;
    };
}
