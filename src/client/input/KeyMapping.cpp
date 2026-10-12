#include "client/input/KeyMapping.hpp"

namespace perlin {
    std::string Binding::Serialize() const {
        switch (device) {
        case Device::Keyboard:
            return KeyName(code);
        case Device::Mouse:
            return MouseName(code);
        default:
            return {};
        }
    }

    Binding Binding::Parse(const std::string& text) {
        if (int key = KeyFromName(text))
            return KeyboardKey(key);
        if (int button = MouseFromName(text); button >= 0)
            return MouseButton(button);
        return {};
    }

    const char* ActionId(Action action) {
        switch (action) {
        case Action::Forward:
            return "key.forward";
        case Action::Back:
            return "key.back";
        case Action::Left:
            return "key.left";
        case Action::Right:
            return "key.right";
        case Action::Jump:
            return "key.jump";
        case Action::Sneak:
            return "key.sneak";
        case Action::Look:
            return "key.look";
        case Action::CyclePerspective:
            return "key.togglePerspective";
        case Action::Screenshot:
            return "key.screenshot";
        case Action::Sprint:
            return "key.sprint";
        default:
            return "";
        }
    }

    KeyBindings DefaultBindings() {
        KeyBindings b{};
        auto set = [&](Action a, Binding v) { b[static_cast<size_t>(a)] = v; };
        set(Action::Forward, Binding::KeyboardKey('W'));
        set(Action::Back, Binding::KeyboardKey('S'));
        set(Action::Left, Binding::KeyboardKey('A'));
        set(Action::Right, Binding::KeyboardKey('D'));
        set(Action::Jump, Binding::KeyboardKey(Key::Space));
        set(Action::Sneak, Binding::KeyboardKey(Key::Shift));
        set(Action::Look, Binding::MouseButton(Mouse::Left));
        set(Action::CyclePerspective, Binding::KeyboardKey(Key::F1 + 4));
        set(Action::Screenshot, Binding::KeyboardKey(Key::F12));
        set(Action::Sprint, Binding::KeyboardKey(Key::Control));
        return b;
    }
}
