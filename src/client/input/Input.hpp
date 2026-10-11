#pragma once
#include "client/input/Keys.hpp"

#include <atomic>
#include <concurrentqueue.h>

namespace perlin {
    struct InputEvent {
        enum Type { KeyDown, KeyUp, MouseMove, MouseDown, MouseUp };
        Type type;
        int key;
        float x, y;
    };

    class Input {
    public:
        void OnKeyDown(int key);
        void OnKeyUp(int key);
        void OnMouseMove(float x, float y);
        void OnMouseDown(int button);
        void OnMouseUp(int button);
        void ReleaseAll();

        bool IsKeyDown(int key) const;
        bool IsMouseDown(int button) const;
        float MouseX() const { return mouseX.load(std::memory_order_relaxed); }
        float MouseY() const { return mouseY.load(std::memory_order_relaxed); }

        moodycamel::ConcurrentQueue<InputEvent> events;

    private:
        std::atomic<bool> keys[256]{};
        std::atomic<bool> buttons[8]{};
        std::atomic<float> mouseX{0}, mouseY{0};
    };
}
