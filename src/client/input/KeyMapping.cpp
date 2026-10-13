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
        case Action::Zoom:
            return "key.zoom";
        case Action::Attack:
            return "key.attack";
        case Action::Use:
            return "key.use";
        case Action::Hotbar1: return "key.hotbar.1";
        case Action::Hotbar2: return "key.hotbar.2";
        case Action::Hotbar3: return "key.hotbar.3";
        case Action::Hotbar4: return "key.hotbar.4";
        case Action::Hotbar5: return "key.hotbar.5";
        case Action::Hotbar6: return "key.hotbar.6";
        case Action::Hotbar7: return "key.hotbar.7";
        case Action::Hotbar8: return "key.hotbar.8";
        case Action::Hotbar9: return "key.hotbar.9";
        case Action::ToggleHud:
            return "key.toggleHud";
        case Action::Chat:
            return "key.chat";
        case Action::Command:
            return "key.command";
        case Action::DebugScreen:
            return "key.debugScreen";
        case Action::PickBlock:
            return "key.pickItem";
        case Action::Drop:
            return "key.drop";
        case Action::PlayerList:
            return "key.playerlist";
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
        set(Action::Zoom, Binding::KeyboardKey('C'));
        set(Action::Attack, Binding::MouseButton(Mouse::Left));
        set(Action::Use, Binding::MouseButton(Mouse::Right));
        for (int i = 0; i < 9; i++)
            set(static_cast<Action>(static_cast<int>(Action::Hotbar1) + i), Binding::KeyboardKey('1' + i));
        set(Action::ToggleHud, Binding::KeyboardKey(Key::F1));
        set(Action::Chat, Binding::KeyboardKey('T'));
        set(Action::Command, Binding::KeyboardKey(Key::Slash));
        set(Action::DebugScreen, Binding::KeyboardKey(Key::F1 + 2));
        set(Action::PickBlock, Binding::MouseButton(Mouse::Middle));
        set(Action::Drop, Binding::KeyboardKey('Q'));
        set(Action::PlayerList, Binding::KeyboardKey(Key::Tab));
        return b;
    }
}
