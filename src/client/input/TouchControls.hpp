#pragma once
#include "client/input/KeyMapping.hpp"
#include "client/input/TouchState.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace perlin {
    class Input;
    class RenderContext;

    // Touch layout modelled on Minecraft Pocket/Bedrock: a thumb joystick on the left, jump / sneak / attack /
    // place on the right (they change icon when swimming, flying or riding), a row of six system buttons at
    // the top centre, a tappable hotbar with a "..." inventory button, and the rest of the screen turns the
    // camera. Every button presses the action's current key binding, so rebinding keys still works.
    class TouchControls {
    public:
        enum Button {
            Jump,
            Sneak,
            Attack,
            Use,
            Sprint,
            Pause,
            Chat,
            Perspective,
            Debug,
            Players,
            HideHud,
            Inventory,
            ButtonCount
        };

        void SetViewport(int width, int height);
        void SetState(const TouchState& state);
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
            bool moved = false;
            // A menu finger that turned into a vertical drag: it scrolls instead of pressing.
            bool scrolling = false;
            std::chrono::steady_clock::time_point restoreAt;
            bool pointerParked = false;
        };

        enum class Kind { Hold, Tap, SneakKey, SprintToggle };

        struct Layout {
            float cx = 0, cy = 0, r = 0;
            Action action = Action::Count;
            Kind kind = Kind::Hold;
            const char* label = "";
            bool system = false; // small square-ish top/inventory buttons drawn with a dark backing
        };

        struct Sprite {
            uint32_t id = 0;
            int w = 0, h = 0;
            int nextTry = 0;
        };

        void BuildLayout();
        int HitButton(float x, float y) const;
        void PressButton(Input& input, int index);
        void ReleaseButton(Input& input, int index);
        Binding PressAction(Input& input, Action action);
        static void ReleaseBinding(Input& input, const Binding& binding);
        void Tap(Input& input, Action action);
        void ApplyStick(Input& input);
        void ReleaseAll(Input& input);
        bool InHotbar(float x, float y, int& slot) const;
        Finger* Find(int64_t id);
        void Erase(int64_t id);
        void Scroll(Input& input, float dy);

        std::mutex mutex;
        int width = 0, height = 0;
        TouchState state;
        std::array<Layout, ButtonCount> layout{};
        std::array<bool, ButtonCount> down{};
        std::vector<Finger> fingers;
        float hotbarX = 0, hotbarY = 0, hotbarW = 0, hotbarH = 0;

        bool stickActive = false;
        float stickX = 0, stickY = 0, knobX = 0, knobY = 0;
        bool stickHeld[5] = {false, false, false, false, false};
        Binding stickBinding[5];
        bool sneakLatched = false, sneakMomentary = false;
        bool sprintLatched = false;
        bool wasGameplay = false;
        float scrollAccum = 0.0f;

        std::array<Binding, ButtonCount> pressedBinding{};

        std::vector<Sprite> sprites;
        uint32_t discTexture = 0, fontTexture = 0;
        uint32_t fontVersion = 0;
    };
}
