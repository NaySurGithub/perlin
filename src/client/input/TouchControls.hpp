#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace perlin {
    class Input;
    class RenderContext;

    class TouchControls {
    public:
        enum Button {
            Jump,
            Sneak,
            Attack,
            Use,
            Pause,
            Chat,
            Inventory,
            Perspective,
            Previous,
            Next,
            Drop,
            ButtonCount
        };

        void SetViewport(int width, int height);
        void FingerDown(Input& input, int64_t id, float x, float y);
        void FingerMove(Input& input, int64_t id, float x, float y, float dx, float dy);
        void FingerUp(Input& input, int64_t id, float x, float y);
        void Cancel(Input& input);
        void Update(Input& input);
        void Draw(RenderContext& renderer, bool visible);
        bool Gameplay(const Input& input) const;

    private:
        enum class Role { None, Stick, Button, Look, Pointer, Scroll };

        struct Finger {
            int64_t id = 0;
            Role role = Role::None;
            int button = -1;
            float startX = 0, startY = 0, lastX = 0, lastY = 0;
            std::chrono::steady_clock::time_point since;
            bool moved = false;
            bool holding = false;
        };

        struct Layout {
            float cx = 0, cy = 0, r = 0;
            const char* label = "";
            int key = 0;
            int mouse = -1;
            int wheel = 0;
            bool toggle = false;
        };

        void BuildLayout();
        int HitButton(float x, float y) const;
        void PressButton(Input& input, int index);
        void ReleaseButton(Input& input, int index);
        void ApplyStick(Input& input);
        void ReleaseAll(Input& input);
        Finger* Find(int64_t id);
        void Erase(int64_t id);

        std::mutex mutex;
        int width = 0, height = 0;
        std::array<Layout, ButtonCount> layout{};
        std::array<bool, ButtonCount> down{};
        std::vector<Finger> fingers;

        bool stickActive = false;
        int64_t stickId = 0;
        float stickX = 0, stickY = 0, knobX = 0, knobY = 0;
        bool stickKeys[5] = {false, false, false, false, false};
        bool sneakLatched = false;
        bool wasGameplay = false;
        float scrollAccum = 0.0f;

        uint32_t discTexture = 0, fontTexture = 0;
        uint32_t fontVersion = 0;
        int fontW = 0, fontH = 0;
    };
}
