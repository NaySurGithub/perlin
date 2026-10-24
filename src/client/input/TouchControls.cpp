#include "client/input/TouchControls.hpp"

#include "client/input/Input.hpp"
#include "client/input/Keys.hpp"
#include "client/renderer/RenderContext.hpp"
#include "client/ui/McFont.hpp"
#include "client/ui/UiAssets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace perlin {
    namespace {
        using Clock = std::chrono::steady_clock;

        constexpr float kStickRadius = 0.14f;
        constexpr float kDeadZone = 0.22f;
        constexpr float kSprintZone = 0.92f;
        constexpr float kLookScale = 0.55f;
        constexpr float kTapSlop = 18.0f;
        constexpr float kScrollStep = 36.0f;
        constexpr int kDiscTile = 64;

        enum StickKey { KeyForward, KeyBack, KeyLeft, KeyRight, KeySprint };

        enum S {
            Frame, Knob,
            Jump, JumpP, WaterUp, WaterUpP, FlyUp, FlyUpP,
            Sneak, SneakP, WaterDown, WaterDownP, FlyDown, FlyDownP, Dismount, DismountP,
            Attack, AttackP, Interact, InteractP, Sprint, SprintP,
            PauseIcon, ChatIcon, Dots, PerspectiveIcon, DebugIcon, PlayersIcon, HudIcon,
            SpriteCount
        };
        constexpr const char* kSpriteNames[SpriteCount] = {
            "textures/ui/joystick_frame", "textures/ui/joystick_knob",
            "textures/ui/jump", "textures/ui/jump_pressed", "textures/ui/waterascend", "textures/ui/waterascend_pressed",
            "textures/ui/flyingascend", "textures/ui/flyingascend_pressed",
            "textures/ui/sneak", "textures/ui/sneak_pressed", "textures/ui/waterdescend", "textures/ui/waterdescend_pressed",
            "textures/ui/flyingdescend", "textures/ui/flyingdescend_pressed", "textures/ui/dismount", "textures/ui/dismount_pressed",
            "textures/ui/attack", "textures/ui/attack_pressed", "textures/ui/interact", "textures/ui/interact_pressed",
            "textures/ui/sprint", "textures/ui/sprint_pressed",
            "textures/ui/pause_icon", "textures/ui/comment", "textures/ui/more_dots",
            "textures/ui/perspective", "textures/ui/debug_glyph", "textures/ui/icon_multiplayer", "textures/ui/camera-small",
        };
        constexpr const char* kButtonNames[] = {"jump", "sneak", "attack", "use", "sprint", "pause", "chat", "f5", "f3", "tab", "f1", "inventory"};

        uint32_t Pack(float r, float g, float b, float a) {
            const auto byte = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
            return (byte(a) << 24) | (byte(b * a) << 16) | (byte(g * a) << 8) | byte(r * a);
        }

        struct Mesh {
            std::vector<UiVertex> vertices;
            std::vector<uint32_t> indices;

            void Quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t color) {
                const uint32_t base = static_cast<uint32_t>(vertices.size());
                vertices.push_back({x0, y0, 0.0f, color, u0, v0});
                vertices.push_back({x1, y0, 0.0f, color, u1, v0});
                vertices.push_back({x1, y1, 0.0f, color, u1, v1});
                vertices.push_back({x0, y1, 0.0f, color, u0, v1});
                for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                    indices.push_back(base + i);
            }
        };

        void Disc(Mesh& m, float cx, float cy, float r, uint32_t color, bool ring) {
            const float u0 = ring ? 0.5f : 0.0f, u1 = ring ? 1.0f : 0.5f;
            m.Quad(cx - r, cy - r, cx + r, cy + r, u0, 0.0f, u1, 1.0f, color);
        }
    }

    bool TouchControls::Gameplay(const Input& input) const {
        return input.Captured() && !input.TextMode();
    }

    void TouchControls::SetViewport(int w, int h) {
        std::lock_guard lock(mutex);
        if (w == width && h == height)
            return;
        width = w;
        height = h;
        BuildLayout();
    }

    void TouchControls::SetState(const TouchState& s) {
        std::lock_guard lock(mutex);
        const bool relayout = s.hudScale != state.hudScale || s.scale != state.scale || (!editing && s.layout != state.layout);
        const bool startEditing = s.editing && !editing;
        const bool stopEditing = !s.editing && editing;
        state = s;
        if (startEditing) {
            editing = true;
            editDone = false;
            editSelected = -1;
            editLayout = s.layout;
            fingers.clear();
        } else if (!s.editing && editing) {
            editing = false;
            fingers.clear();
        }
        // Geometry only depends on viewport, scale and layout. Rebuilding it
        // every gameplay frame needlessly holds the finger-event mutex.
        // Leaving the editor must restore the saved layout even if unchanged.
        if ((relayout || startEditing || stopEditing) && width > 0)
            BuildLayout();
    }

    bool TouchControls::TakeEditResult(std::map<std::string, TouchPlace>& out) {
        std::lock_guard lock(mutex);
        if (!editDone)
            return false;
        editDone = false;
        out = editLayout;
        return true;
    }

    TouchPlace TouchControls::Place(const char* name) const {
        const auto& source = editing ? editLayout : state.layout;
        const auto it = source.find(name);
        return it == source.end() ? TouchPlace{} : it->second;
    }

    void TouchControls::BuildLayout() {
        const float u = static_cast<float>(std::min(width, height));
        const float W = static_cast<float>(width);
        const float H = static_cast<float>(height);
        const float global = state.scale;
        auto set = [&](Button b, float cx, float cy, float r, Action action, Kind kind, const char* label, bool system) {
            const TouchPlace p = Place(kButtonNames[b]);
            Layout& l = layout[b];
            l.cx = p.x >= 0.0f ? p.x * W : cx;
            l.cy = p.y >= 0.0f ? p.y * H : cy;
            l.r = r * global * std::max(0.4f, p.scale);
            l.action = action;
            l.kind = kind;
            l.label = label;
            l.system = system;
        };
        set(Jump, W - 0.14f * u, H - 0.17f * u, 0.095f * u, Action::Jump, Kind::Hold, "^", false);
        set(Sneak, W - 0.36f * u, H - 0.13f * u, 0.07f * u, Action::Sneak, Kind::SneakKey, "v", false);
        set(Use, W - 0.14f * u, H - 0.42f * u, 0.08f * u, Action::Use, Kind::Hold, "+", false);
        set(Attack, W - 0.36f * u, H - 0.36f * u, 0.085f * u, Action::Attack, Kind::Hold, "X", false);
        set(Sprint, 0.20f * u, H - 0.52f * u, 0.058f * u, Action::Sprint, Kind::SprintToggle, ">>", false);

        const float r = 0.043f * u, pitch = 0.112f * u;
        const Button top[6] = {Pause, Chat, Perspective, Debug, Players, HideHud};
        const Action topAction[6] = {Action::Pause, Action::Chat, Action::CyclePerspective, Action::DebugScreen, Action::PlayerList, Action::ToggleHud};
        const char* topLabel[6] = {"II", "T", "F5", "F3", "Tab", "F1"};
        for (int i = 0; i < 6; i++)
            set(top[i], W * 0.5f + (static_cast<float>(i) - 2.5f) * pitch, r + 0.02f * u, r, topAction[i], topAction[i] == Action::PlayerList ? Kind::Hold : Kind::Tap, topLabel[i], true);

        const float scale = state.hudScale > 0 ? static_cast<float>(state.hudScale) : std::max(1.0f, std::floor(u / 240.0f));
        hotbarW = 182.0f * scale;
        hotbarH = 22.0f * scale;
        hotbarX = W * 0.5f - hotbarW * 0.5f;
        hotbarY = H - hotbarH;
        const float ir = std::max(0.035f * u, 10.0f * scale);
        set(Inventory, hotbarX + hotbarW + ir * 1.3f, H - hotbarH * 0.5f - 2.0f * scale, ir, Action::Inventory, Kind::Tap, "...", true);

        const TouchPlace sp = Place("stick");
        stickR = kStickRadius * u * global * std::max(0.4f, sp.scale);
        stickCX = sp.x >= 0.0f ? sp.x * W : 0.20f * u;
        stickCY = sp.y >= 0.0f ? sp.y * H : H - 0.20f * u;
    }

    namespace {
        struct ToolBox { float x, y, w, h; const char* label; };
    }

    static void ToolBoxes(int width, int height, ToolBox out[4]) {
        const float u = static_cast<float>(std::min(width, height));
        const float w = 0.15f * u, h = 0.075f * u, gap = 0.02f * u;
        const float total = 4 * w + 3 * gap;
        const float x0 = static_cast<float>(width) * 0.5f - total * 0.5f;
        const float y = static_cast<float>(height) * 0.5f - h * 0.5f;
        const char* labels[4] = {"-", "+", "Reset", "Done"};
        for (int i = 0; i < 4; i++)
            out[i] = {x0 + static_cast<float>(i) * (w + gap), y, w, h, labels[i]};
    }

    int TouchControls::EditorHit(float x, float y) const {
        ToolBox boxes[4];
        ToolBoxes(width, height, boxes);
        for (int i = 0; i < 4; i++)
            if (x >= boxes[i].x && x < boxes[i].x + boxes[i].w && y >= boxes[i].y && y < boxes[i].y + boxes[i].h)
                return i;
        return -1;
    }

    void TouchControls::EditorPress(int which) {
        if (which == 0 || which == 1) {
            if (editSelected < 0)
                return;
            const char* name = editSelected == ButtonCount ? "stick" : kButtonNames[editSelected];
            TouchPlace p = editLayout.count(name) ? editLayout[name] : TouchPlace{};
            p.scale = std::clamp(p.scale * (which == 1 ? 1.12f : 1.0f / 1.12f), 0.4f, 2.5f);
            if (p.x < 0.0f && editSelected != ButtonCount) {
                p.x = layout[editSelected].cx / static_cast<float>(width);
                p.y = layout[editSelected].cy / static_cast<float>(height);
            } else if (p.x < 0.0f) {
                p.x = stickCX / static_cast<float>(width);
                p.y = stickCY / static_cast<float>(height);
            }
            editLayout[name] = p;
            BuildLayout();
        } else if (which == 2) {
            editLayout.clear();
            editSelected = -1;
            BuildLayout();
        } else if (which == 3) {
            editDone = true;
            editing = false;
            fingers.clear();
        }
    }

    bool TouchControls::InHotbar(float x, float y, int& slot) const {
        if (hotbarW <= 0)
            return false;
        const float margin = hotbarH * 0.3f;
        if (x < hotbarX || x >= hotbarX + hotbarW || y < hotbarY - margin)
            return false;
        const float unit = hotbarW / 182.0f * 20.0f;
        slot = std::clamp(static_cast<int>((x - hotbarX - hotbarW / 182.0f) / unit), 0, 8);
        return true;
    }

    int TouchControls::HitButton(float x, float y) const {
        int best = -1;
        float bestD = 1e30f;
        for (int i = 0; i < ButtonCount; i++) {
            const Layout& l = layout[i];
            const float dx = x - l.cx, dy = y - l.cy;
            const float r = l.r * 1.2f;
            const float d = dx * dx + dy * dy;
            if (d <= r * r && d < bestD) {
                best = i;
                bestD = d;
            }
        }
        return best;
    }

    TouchControls::Finger* TouchControls::Find(int64_t id) {
        for (Finger& f : fingers)
            if (f.id == id)
                return &f;
        return nullptr;
    }

    void TouchControls::Erase(int64_t id) {
        fingers.erase(std::remove_if(fingers.begin(), fingers.end(), [&](const Finger& f) { return f.id == id; }), fingers.end());
    }

    Binding TouchControls::PressAction(Input& input, Action action) {
        const Binding b = state.bindings[static_cast<size_t>(action)];
        if (b.device == Binding::Device::Keyboard)
            input.OnKeyDown(b.code);
        else if (b.device == Binding::Device::Mouse)
            input.OnMouseDown(b.code);
        return b;
    }

    void TouchControls::ReleaseBinding(Input& input, const Binding& b) {
        if (b.device == Binding::Device::Keyboard)
            input.OnKeyUp(b.code);
        else if (b.device == Binding::Device::Mouse)
            input.OnMouseUp(b.code);
    }

    void TouchControls::Tap(Input& input, Action action) {
        Binding b;
        if (action == Action::DebugScreen) {
            b = Binding::KeyboardKey(Key::F3);
            input.OnKeyDown(Key::F3);
        } else {
            b = PressAction(input, action);
        }
        if (b.device == Binding::Device::None)
            return;
        pendingReleases.push_back({b, Clock::now() + std::chrono::milliseconds(120)});
    }

    void TouchControls::FlushReleases(Input& input, bool all) {
        const auto now = Clock::now();
        for (size_t i = 0; i < pendingReleases.size();) {
            if (all || now >= pendingReleases[i].at) {
                ReleaseBinding(input, pendingReleases[i].binding);
                pendingReleases.erase(pendingReleases.begin() + static_cast<long>(i));
            } else {
                i++;
            }
        }
    }

    void TouchControls::PressButton(Input& input, int index) {
        const Layout& l = layout[index];
        switch (l.kind) {
        case Kind::SneakKey: {
            const bool momentary = state.flying || state.inWater || state.riding;
            if (momentary) {
                if (!sneakLatched)
                    pressedBinding[index] = PressAction(input, l.action);
                sneakMomentary = true;
                down[index] = true;
            } else {
                sneakLatched = !sneakLatched;
                if (sneakLatched)
                    pressedBinding[index] = PressAction(input, l.action);
                else
                    ReleaseBinding(input, pressedBinding[index]);
                down[index] = sneakLatched;
            }
            return;
        }
        case Kind::SprintToggle:
            sprintLatched = !sprintLatched;
            down[index] = sprintLatched;
            ApplyStick(input);
            return;
        case Kind::Tap:
            down[index] = true;
            Tap(input, l.action);
            return;
        case Kind::Hold:
            down[index] = true;
            pressedBinding[index] = PressAction(input, l.action);
            return;
        }
    }

    void TouchControls::ReleaseButton(Input& input, int index) {
        const Layout& l = layout[index];
        switch (l.kind) {
        case Kind::SneakKey:
            if (sneakMomentary) {
                sneakMomentary = false;
                if (!sneakLatched)
                    ReleaseBinding(input, pressedBinding[index]);
                down[index] = sneakLatched;
            }
            return;
        case Kind::SprintToggle:
            return;
        case Kind::Tap:
            down[index] = false;
            return;
        case Kind::Hold:
            down[index] = false;
            ReleaseBinding(input, pressedBinding[index]);
            return;
        }
    }

    void TouchControls::ApplyStick(Input& input) {
        bool want[5] = {false, false, false, false, false};
        if (stickActive) {
            const float radius = std::max(1.0f, stickR);
            const float nx = knobX / radius, ny = knobY / radius;
            const float len = std::sqrt(nx * nx + ny * ny);
            if (len > kDeadZone) {
                want[KeyForward] = ny < -0.38f;
                want[KeyBack] = ny > 0.38f;
                want[KeyLeft] = nx < -0.38f;
                want[KeyRight] = nx > 0.38f;
                want[KeySprint] = len >= kSprintZone && ny < -0.5f;
            }
        }
        want[KeySprint] = want[KeySprint] || sprintLatched;
        static constexpr Action kActions[5] = {Action::Forward, Action::Back, Action::Left, Action::Right, Action::Sprint};
        for (int i = 0; i < 5; i++) {
            if (want[i] == stickHeld[i])
                continue;
            stickHeld[i] = want[i];
            if (want[i])
                stickBinding[i] = PressAction(input, kActions[i]);
            else
                ReleaseBinding(input, stickBinding[i]);
        }
    }

    void TouchControls::ReleaseAll(Input& input) {
        FlushReleases(input, true);
        stickActive = false;
        knobX = knobY = 0.0f;
        sprintLatched = false;
        down[Sprint] = false;
        ApplyStick(input);
        for (int i = 0; i < ButtonCount; i++)
            if (down[i] && layout[i].kind == Kind::Hold)
                ReleaseButton(input, i);
        for (Finger& f : fingers)
            if (f.role == Role::Pointer && !f.scrolling)
                input.OnMouseUp(Mouse::Left);
        if (sneakLatched || sneakMomentary) {
            ReleaseBinding(input, pressedBinding[Sneak]);
            sneakLatched = sneakMomentary = false;
            down[Sneak] = false;
        }
        down.fill(false);
        fingers.clear();
    }

    void TouchControls::Cancel(Input& input) {
        std::lock_guard lock(mutex);
        ReleaseAll(input);
    }

    void TouchControls::FingerDown(Input& input, int64_t id, float x, float y) {
        std::lock_guard lock(mutex);
        Finger f;
        f.id = id;
        f.startX = f.lastX = x;
        f.startY = f.lastY = y;
        if (editing) {
            const int tool = EditorHit(x, y);
            if (tool >= 0) {
                f.role = Role::EditorButton;
                f.button = tool;
                EditorPress(tool);
            } else {
                const int hit = HitButton(x, y);
                const float dx = x - stickCX, dy = y - stickCY;
                if (hit >= 0) {
                    editSelected = hit;
                    f.role = Role::Drag;
                    f.grabX = layout[hit].cx - x;
                    f.grabY = layout[hit].cy - y;
                } else if (dx * dx + dy * dy <= stickR * stickR * 2.25f) {
                    editSelected = ButtonCount;
                    f.role = Role::Drag;
                    f.grabX = stickCX - x;
                    f.grabY = stickCY - y;
                } else {
                    editSelected = -1;
                }
            }
            fingers.push_back(f);
            return;
        }
        if (!Gameplay(input)) {
            const bool havePointer = std::any_of(fingers.begin(), fingers.end(), [](const Finger& o) { return o.role == Role::Pointer; });
            if (!havePointer) {
                f.role = Role::Pointer;
                input.OnMouseMove(x, y);
                input.OnMouseDown(Mouse::Left);
            } else {
                f.role = Role::Scroll;
            }
            fingers.push_back(f);
            return;
        }
        int slot = 0;
        const int hit = HitButton(x, y);
        if (hit >= 0) {
            f.role = Role::Button;
            f.button = hit;
            PressButton(input, hit);
        } else if (InHotbar(x, y, slot)) {
            Tap(input, static_cast<Action>(static_cast<int>(Action::Hotbar1) + slot));
            f.role = Role::None;
        } else if (!stickActive && (x - stickCX) * (x - stickCX) + (y - stickCY) * (y - stickCY) <= stickR * stickR * 2.56f) {
            f.role = Role::Stick;
            stickActive = true;
            stickX = stickCX;
            stickY = stickCY;
            knobX = x - stickCX;
            knobY = y - stickCY;
            const float len = std::sqrt(knobX * knobX + knobY * knobY);
            if (len > stickR) {
                knobX *= stickR / len;
                knobY *= stickR / len;
            }
            ApplyStick(input);
        } else {
            f.role = Role::Look;
        }
        fingers.push_back(f);
    }

    void TouchControls::Scroll(Input& input, float dy) {
        scrollAccum += dy;
        while (scrollAccum >= kScrollStep) {
            input.OnMouseWheel(1.0f);
            scrollAccum -= kScrollStep;
        }
        while (scrollAccum <= -kScrollStep) {
            input.OnMouseWheel(-1.0f);
            scrollAccum += kScrollStep;
        }
    }

    void TouchControls::FingerMove(Input& input, int64_t id, float x, float y, float dx, float dy) {
        std::lock_guard lock(mutex);
        Finger* f = Find(id);
        if (!f)
            return;
        const float fromStartX = x - f->startX, fromStartY = y - f->startY;
        if (fromStartX * fromStartX + fromStartY * fromStartY > kTapSlop * kTapSlop)
            f->moved = true;
        f->lastX = x;
        f->lastY = y;
        switch (f->role) {
        case Role::Pointer:
            if (!f->scrolling && f->moved && std::fabs(fromStartY) > std::fabs(fromStartX) * 1.2f) {
                f->scrolling = true;
                f->pointerParked = true;
                f->restoreAt = Clock::now() + std::chrono::milliseconds(120);
                scrollAccum = 0.0f;
                input.OnMouseMove(-10000.0f, -10000.0f);
                input.OnMouseUp(Mouse::Left);
            }
            if (f->scrolling) {
                Scroll(input, dy);
                if (f->pointerParked && Clock::now() >= f->restoreAt) {
                    f->pointerParked = false;
                    input.OnMouseMove(x, y);
                }
            } else {
                input.OnMouseMove(x, y);
            }
            break;
        case Role::Scroll:
            Scroll(input, dy);
            break;
        case Role::Stick: {
            const float radius = std::max(1.0f, stickR);
            float ox = x - stickCX, oy = y - stickCY;
            const float len = std::sqrt(ox * ox + oy * oy);
            if (len > radius) {
                ox *= radius / len;
                oy *= radius / len;
            }
            knobX = ox;
            knobY = oy;
            ApplyStick(input);
            break;
        }
        case Role::Button:
            if (f->button == Attack || f->button == Use || f->button == Jump)
                input.OnMouseDelta(dx * kLookScale * state.sensitivity, dy * kLookScale * state.sensitivity);
            break;
        case Role::Look:
            input.OnMouseDelta(dx * kLookScale * state.sensitivity, dy * kLookScale * state.sensitivity);
            break;
        case Role::Drag: {
            if (editSelected < 0)
                break;
            const char* name = editSelected == ButtonCount ? "stick" : kButtonNames[editSelected];
            TouchPlace p = editLayout.count(name) ? editLayout[name] : TouchPlace{};
            p.x = std::clamp((x + f->grabX) / static_cast<float>(width), 0.02f, 0.98f);
            p.y = std::clamp((y + f->grabY) / static_cast<float>(height), 0.03f, 0.97f);
            editLayout[name] = p;
            BuildLayout();
            break;
        }
        default:
            break;
        }
    }

    void TouchControls::FingerUp(Input& input, int64_t id, float x, float y) {
        std::lock_guard lock(mutex);
        Finger* f = Find(id);
        if (!f)
            return;
        switch (f->role) {
        case Role::Pointer:
            input.OnMouseMove(x, y);
            if (!f->scrolling)
                input.OnMouseUp(Mouse::Left);
            break;
        case Role::Stick:
            stickActive = false;
            knobX = knobY = 0.0f;
            sprintLatched = false;
            down[Sprint] = false;
            ApplyStick(input);
            break;
        case Role::Button:
            ReleaseButton(input, f->button);
            break;
        default:
            break;
        }
        Erase(id);
    }

    void TouchControls::Update(Input& input) {
        std::lock_guard lock(mutex);
        FlushReleases(input, false);
        if (editing)
            return;
        const bool gameplay = Gameplay(input);
        if (wasGameplay != gameplay) {
            wasGameplay = gameplay;
            ReleaseAll(input);
            return;
        }
        if (!gameplay) {
            const auto t = Clock::now();
            for (Finger& f : fingers)
                if (f.role == Role::Pointer && f.pointerParked && t >= f.restoreAt) {
                    f.pointerParked = false;
                    input.OnMouseMove(f.lastX, f.lastY);
                }
        }
    }

    void TouchControls::Draw(RenderContext& renderer, bool visible) {
        std::lock_guard lock(mutex);
        if ((!visible && !editing) || width <= 0 || height <= 0)
            return;

        if (!discTexture) {
            std::vector<uint8_t> pixels(static_cast<size_t>(kDiscTile) * 2 * kDiscTile * 4, 0);
            for (int y = 0; y < kDiscTile; y++) {
                for (int x = 0; x < kDiscTile * 2; x++) {
                    const bool ring = x >= kDiscTile;
                    const float px = (static_cast<float>(ring ? x - kDiscTile : x) + 0.5f) / kDiscTile * 2.0f - 1.0f;
                    const float py = (static_cast<float>(y) + 0.5f) / kDiscTile * 2.0f - 1.0f;
                    const float d = std::sqrt(px * px + py * py);
                    float a = 0.0f;
                    if (ring) {
                        const float outer = std::clamp((1.0f - d) * 16.0f, 0.0f, 1.0f);
                        const float inner = std::clamp((d - 0.86f) * 16.0f, 0.0f, 1.0f);
                        a = outer * inner;
                    } else {
                        a = std::clamp((1.0f - d) * 16.0f, 0.0f, 1.0f);
                    }
                    uint8_t* p = &pixels[(static_cast<size_t>(y) * kDiscTile * 2 + x) * 4];
                    const uint8_t v = static_cast<uint8_t>(a * 255.0f + 0.5f);
                    p[0] = p[1] = p[2] = v;
                    p[3] = v;
                }
            }
            discTexture = renderer.UiCreateTexture(kDiscTile * 2, kDiscTile, pixels.data(), true);
        }
        const ui::McFont& font = ui::McFont::Instance();
        if (!fontTexture || fontVersion != font.Version()) {
            if (fontTexture)
                renderer.UiDestroyTexture(fontTexture);
            std::vector<uint8_t> atlas = font.Atlas();
            for (size_t i = 0; i + 3 < atlas.size(); i += 4) {
                const unsigned a = atlas[i + 3];
                atlas[i] = static_cast<uint8_t>((atlas[i] * a + 127) / 255);
                atlas[i + 1] = static_cast<uint8_t>((atlas[i + 1] * a + 127) / 255);
                atlas[i + 2] = static_cast<uint8_t>((atlas[i + 2] * a + 127) / 255);
            }
            fontTexture = renderer.UiCreateTexture(font.AtlasWidth(), font.AtlasHeight(), atlas.data(), false);
            fontVersion = font.Version();
        }

        if (sprites.empty())
            sprites.resize(SpriteCount);
        for (int i = 0; i < SpriteCount; i++) {
            Sprite& sp = sprites[i];
            if (sp.id) {
                continue;
            }
            if (sp.nextTry > 0) {
                sp.nextTry--;
                continue;
            }
            Texture t;
            if (!ui::UiAssets::Instance().Image(kSpriteNames[i], t) || t.width <= 0 || t.height <= 0 ||
                t.pixels.size() < static_cast<size_t>(t.width) * t.height * 4) {
                sp.nextTry = 120;
                continue;
            }
            for (size_t k = 0; k + 3 < t.pixels.size(); k += 4) {
                const unsigned a = t.pixels[k + 3];
                t.pixels[k] = static_cast<unsigned char>((t.pixels[k] * a + 127) / 255);
                t.pixels[k + 1] = static_cast<unsigned char>((t.pixels[k + 1] * a + 127) / 255);
                t.pixels[k + 2] = static_cast<unsigned char>((t.pixels[k + 2] * a + 127) / 255);
            }
            sp.w = t.width;
            sp.h = t.height;
            sp.id = renderer.UiCreateTexture(t.width, t.height, t.pixels.data(), false);
        }

        const float u = static_cast<float>(std::min(width, height));
        Mesh discs, text;
        std::vector<Mesh> icons(SpriteCount);
        auto icon = [&](int sprite, float cx, float cy, float halfW, float halfH, float alpha) {
            icons[sprite].Quad(cx - halfW, cy - halfH, cx + halfW, cy + halfH, 0.0f, 0.0f, 1.0f, 1.0f, Pack(1, 1, 1, alpha));
        };
        auto label = [&](const char* str, float cx, float cy, float scale, float alpha) {
            const std::string s = str;
            float total = 0.0f;
            size_t k = 0;
            uint32_t cp = 0;
            std::vector<uint32_t> cps;
            while (k < s.size() && ui::McFont::Next(s, k, cp)) {
                cps.push_back(cp);
                total += static_cast<float>(font.Get(cp).advance) * scale;
            }
            float pen = cx - total * 0.5f;
            const float top = cy - 4.0f * scale;
            for (uint32_t c : cps) {
                const ui::McFont::Glyph& g = font.Get(c);
                if (g.width > 0)
                    text.Quad(pen, top + g.top * scale, pen + g.width * scale, top + (g.top + g.height) * scale, g.u0, g.v0, g.u1, g.v1, Pack(1, 1, 1, alpha));
                pen += static_cast<float>(g.advance) * scale;
            }
        };
        auto have = [&](int sprite) { return sprites[sprite].id != 0; };

        const float base = std::clamp(state.opacity, 0.2f, 1.0f);
        {
            const float radius = stickR;
            const float cx = stickCX, cy = stickCY;
            const float a = stickActive ? 1.0f : 0.8f;
            if (have(Frame))
                icon(Frame, cx, cy, radius, radius, base * a);
            else {
                Disc(discs, cx, cy, radius, Pack(1, 1, 1, 0.10f * base), false);
                Disc(discs, cx, cy, radius, Pack(1, 1, 1, 0.45f * base), true);
            }
            const float kr = radius * 0.46f;
            const float kx = cx + (stickActive ? knobX : 0.0f), ky = cy + (stickActive ? knobY : 0.0f);
            if (have(Knob))
                icon(Knob, kx, ky, kr, kr, base * (stickActive ? 1.0f : 0.85f));
            else
                Disc(discs, kx, ky, kr, Pack(1, 1, 1, 0.40f * base), false);
            if (editing && editSelected == ButtonCount)
                Disc(discs, cx, cy, radius * 1.08f, Pack(1, 1, 0.3f, 0.95f), true);
        }

        for (int i = 0; i < ButtonCount; i++) {
            const Layout& l = layout[i];
            const bool active = down[i];
            const float alpha = (active ? 1.0f : 0.9f) * base;
            int sprite = -1;
            int glyph = -1;
            switch (i) {
            case Jump:
                sprite = state.inWater ? (active ? WaterUpP : WaterUp) : state.flying ? (active ? FlyUpP : FlyUp) : (active ? JumpP : Jump);
                break;
            case Sneak:
                sprite = state.riding ? (active ? DismountP : Dismount)
                       : state.flying ? (active ? FlyDownP : FlyDown)
                       : state.inWater ? (active ? WaterDownP : WaterDown) : (active ? SneakP : Sneak);
                break;
            case Attack: sprite = active ? AttackP : Attack; break;
            case Use: sprite = active ? InteractP : Interact; break;
            case Sprint: sprite = active ? SprintP : Sprint; break;
            case Perspective: sprite = PerspectiveIcon; break;
            case Pause: glyph = PauseIcon; break;
            case Chat: glyph = ChatIcon; break;
            case Debug: glyph = DebugIcon; break;
            case Players: glyph = PlayersIcon; break;
            case HideHud: glyph = HudIcon; break;
            case Inventory: glyph = Dots; break;
            default: break;
            }
            const bool selected = editing && editSelected == i;
            if (sprite >= 0 && have(sprite)) {
                icon(sprite, l.cx, l.cy, l.r, l.r, alpha);
                if (active && i == Perspective)
                    Disc(discs, l.cx, l.cy, l.r, Pack(1, 1, 1, 0.25f), false);
            } else if (glyph >= 0 && have(Knob) && have(glyph)) {
                icon(Knob, l.cx, l.cy, l.r, l.r, alpha);
                if (active)
                    icon(Knob, l.cx, l.cy, l.r, l.r, 0.35f);
                const Sprite& sp = sprites[glyph];
                const float k = std::max(1.0f, std::floor(l.r * 1.1f / std::max(sp.w, sp.h)));
                icon(glyph, l.cx, l.cy, sp.w * k * 0.5f, sp.h * k * 0.5f, 1.0f);
            } else {
                const float shade = active ? 1.0f : 0.1f;
                Disc(discs, l.cx, l.cy, l.r, Pack(shade, shade, shade, (active ? 0.55f : (l.system ? 0.55f : 0.32f)) * base), false);
                Disc(discs, l.cx, l.cy, l.r, Pack(1, 1, 1, active ? 0.9f : 0.55f), true);
                label(l.label, l.cx, l.cy, std::max(1.0f, std::floor(l.r / (l.system ? 11.0f : 9.0f))), 0.95f);
            }
            if (selected)
                Disc(discs, l.cx, l.cy, l.r * 1.12f, Pack(1, 1, 0.3f, 0.95f), true);
        }

        if (editing) {
            ToolBox boxes[4];
            ToolBoxes(width, height, boxes);
            for (const ToolBox& b : boxes) {
                const float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f;
                if (have(Knob))
                    icons[Knob].Quad(b.x, b.y, b.x + b.w, b.y + b.h, 0.0f, 0.0f, 1.0f, 1.0f, Pack(1, 1, 1, 0.95f));
                else
                    Disc(discs, cx, cy, b.h * 0.5f, Pack(0.1f, 0.1f, 0.1f, 0.7f), false);
                label(b.label, cx, cy, std::max(1.0f, std::floor(b.h / 22.0f)), 1.0f);
            }
            label("Drag buttons to move them. Select one and use - / + to resize.", static_cast<float>(width) * 0.5f,
                  static_cast<float>(height) * 0.5f - u * 0.12f, std::max(1.0f, std::floor(u / 220.0f)), 1.0f);
        }

        auto submit = [&](Mesh& mesh, uint32_t texture) {
            if (mesh.vertices.empty() || !texture)
                return;
            const uint32_t id = renderer.UiCreateFrameMesh(mesh.vertices.data(), static_cast<uint32_t>(mesh.vertices.size()),
                                                           mesh.indices.data(), static_cast<uint32_t>(mesh.indices.size()));
            if (!id)
                return;
            UiDraw draw;
            draw.mesh = id;
            draw.texture = texture;
            draw.premultiplied = true;
            renderer.UiSubmit(UiLayer::Top, draw);
            renderer.UiDestroyMesh(id);
        };
        submit(discs, discTexture);
        for (int i = 0; i < SpriteCount; i++)
            submit(icons[i], sprites[i].id);
        submit(text, fontTexture);
    }
}
