#pragma once
#include "client/input/Keys.hpp"

#include <atomic>
#include <concurrentqueue.h>

namespace perlin {
    struct InputEvent {
        enum Type { KeyDown, KeyUp, MouseMove, MouseDown, MouseUp, KeyRepeat, Char };
        Type type;
        int key;
        float x, y;
    };

    inline void AtomicAdd(std::atomic<float>& target, float value) {
        float expected = target.load(std::memory_order_relaxed);
        while (!target.compare_exchange_weak(expected, expected + value, std::memory_order_relaxed)) {
        }
    }

    class Input {
    public:
        void OnKeyDown(int key);
        void OnKeyUp(int key);
        void OnMouseMove(float x, float y);
        void OnMouseDown(int button);
        void OnMouseUp(int button);
        void OnMouseDelta(float dx, float dy) {
            AtomicAdd(lookX, dx);
            AtomicAdd(lookY, dy);
        }
        void TakeMouseDelta(float& dx, float& dy) {
            dx = lookX.exchange(0.0f, std::memory_order_relaxed);
            dy = lookY.exchange(0.0f, std::memory_order_relaxed);
        }
        void OnMouseWheel(float dy) { AtomicAdd(wheel, dy); }
        float TakeWheel() { return wheel.exchange(0.0f, std::memory_order_relaxed); }
        void SetCaptured(bool c) { captured = c; }
        bool Captured() const { return captured; }
        bool pauseOnLostFocus = true;
        void OnKeyRepeat(int key);
        void OnChar(int codepoint);
        void SetTextMode(bool on) { textMode = on; }
        bool TextMode() const { return textMode; }
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
        std::atomic<float> lookX{0}, lookY{0};
        std::atomic<float> wheel{0};
        std::atomic<bool> captured{false};
        std::atomic<bool> textMode{false};
    };
}
