#include "client/input/InputManager.hpp"

#include <algorithm>

namespace perlin {
    bool InputManager::Matches(Action action, const Binding& binding) const {
        return options.GetKey(action) == binding;
    }

    void InputManager::Dispatch(const Binding& binding) {
        if (rebinding) {
            if (binding.device == Binding::Device::Keyboard && binding.code == Key::Escape) {
                rebinding.reset();
                return;
            }
            options.SetKey(*rebinding, binding);
            rebinding.reset();
            options.Save();
            return;
        }
        for (size_t i = 0; i < kActionCount; i++)
            if (Matches(static_cast<Action>(i), binding))
                pressed.set(i);
    }

    void InputManager::Poll() {
        pressed.reset();
        InputEvent ev;
        while (input.events.try_dequeue(ev)) {
            if (ev.type == InputEvent::KeyDown)
                Dispatch(Binding::KeyboardKey(ev.key));
            else if (ev.type == InputEvent::MouseDown)
                Dispatch(Binding::MouseButton(ev.key));
        }

        const float mx = input.MouseX();
        const float my = input.MouseY();
        look = {};
        float dx = 0, dy = 0;
        input.TakeMouseDelta(dx, dy);
        if (input.Captured()) {
            const float sens = options.mouseSensitivity;
            look.yaw = dx * sens;
            look.pitch = dy * sens * (options.invertMouse ? 1.0f : -1.0f);
        } else if (haveMouse && IsDown(Action::Look)) {
            const float sens = options.mouseSensitivity;
            look.yaw = (mx - lastMouseX) * sens;
            look.pitch = (my - lastMouseY) * sens * (options.invertMouse ? 1.0f : -1.0f);
        }
        lastMouseX = mx;
        lastMouseY = my;
        haveMouse = true;
    }

    bool InputManager::IsDown(Action action) const {
        const Binding& b = options.GetKey(action);
        switch (b.device) {
        case Binding::Device::Keyboard:
            return input.IsKeyDown(b.code);
        case Binding::Device::Mouse:
            return input.IsMouseDown(b.code);
        default:
            return false;
        }
    }

    float InputManager::Axis(Action positive, Action negative) const {
        return (IsDown(positive) ? 1.0f : 0.0f) - (IsDown(negative) ? 1.0f : 0.0f);
    }
}
