#include "client/input/TouchControls.hpp"

#include "client/input/Input.hpp"
#include "client/input/Keys.hpp"
#include "client/renderer/RenderContext.hpp"
#include "client/ui/McFont.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace perlin {
    namespace {
        using Clock = std::chrono::steady_clock;

        constexpr float kStickRadius = 0.15f;
        constexpr float kDeadZone = 0.22f;
        constexpr float kSprintZone = 0.92f;
        constexpr float kLookScale = 0.55f;
        constexpr float kTapSlop = 18.0f;
        constexpr double kHoldSeconds = 0.28;
        constexpr double kTapSeconds = 0.30;
        constexpr int kDiscTile = 64;

        enum StickKey { KeyForward, KeyBack, KeyLeft, KeyRight, KeySprint };

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

    void TouchControls::BuildLayout() {
        const float u = static_cast<float>(std::min(width, height));
        const float W = static_cast<float>(width);
        const float H = static_cast<float>(height);
        auto set = [&](Button b, float cx, float cy, float r, const char* label, int key, int mouse, int wheel, bool toggle) {
            Layout& l = layout[b];
            l.cx = cx;
            l.cy = cy;
            l.r = r;
            l.label = label;
            l.key = key;
            l.mouse = mouse;
            l.wheel = wheel;
            l.toggle = toggle;
        };
        set(Jump, W - 0.17f * u, H - 0.17f * u, 0.10f * u, "^", Key::Space, -1, 0, false);
        set(Sneak, W - 0.17f * u, H - 0.42f * u, 0.075f * u, "v", Key::Shift, -1, 0, true);
        set(Attack, W - 0.42f * u, H - 0.13f * u, 0.085f * u, "X", 0, Mouse::Left, 0, false);
        set(Use, W - 0.40f * u, H - 0.36f * u, 0.085f * u, "+", 0, Mouse::Right, 0, false);
        set(Pause, 0.07f * u, 0.07f * u, 0.05f * u, "II", Key::Escape, -1, 0, false);
        set(Chat, 0.19f * u, 0.07f * u, 0.05f * u, "T", 'T', -1, 0, false);
        set(Inventory, W * 0.5f, 0.07f * u, 0.06f * u, "E", 'E', -1, 0, false);
        set(Perspective, W - 0.07f * u, 0.07f * u, 0.05f * u, "F5", Key::F1 + 4, -1, 0, false);
        set(Previous, W * 0.5f - 0.17f * u, 0.07f * u, 0.05f * u, "<", 0, -1, 1, false);
        set(Next, W * 0.5f + 0.17f * u, 0.07f * u, 0.05f * u, ">", 0, -1, -1, false);
        set(Drop, W - 0.07f * u, 0.21f * u, 0.05f * u, "Q", 'Q', -1, 0, false);
    }

    int TouchControls::HitButton(float x, float y) const {
        for (int i = 0; i < ButtonCount; i++) {
            const Layout& l = layout[i];
            const float dx = x - l.cx, dy = y - l.cy;
            const float r = l.r * 1.15f;
            if (dx * dx + dy * dy <= r * r)
                return i;
        }
        return -1;
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

    void TouchControls::PressButton(Input& input, int index) {
        const Layout& l = layout[index];
        if (l.toggle) {
            sneakLatched = !sneakLatched;
            if (sneakLatched)
                input.OnKeyDown(l.key);
            else
                input.OnKeyUp(l.key);
            down[index] = sneakLatched;
            return;
        }
        down[index] = true;
        if (l.wheel != 0) {
            input.OnMouseWheel(static_cast<float>(l.wheel));
            return;
        }
        if (l.key)
            input.OnKeyDown(l.key);
        if (l.mouse >= 0)
            input.OnMouseDown(l.mouse);
    }

    void TouchControls::ReleaseButton(Input& input, int index) {
        const Layout& l = layout[index];
        if (l.toggle)
            return;
        down[index] = false;
        if (l.wheel != 0)
            return;
        if (l.key)
            input.OnKeyUp(l.key);
        if (l.mouse >= 0)
            input.OnMouseUp(l.mouse);
    }

    void TouchControls::ApplyStick(Input& input) {
        bool want[5] = {false, false, false, false, false};
        if (stickActive) {
            const float radius = kStickRadius * static_cast<float>(std::min(width, height));
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
        static constexpr int kKeys[5] = {'W', 'S', 'A', 'D', Key::Control};
        for (int i = 0; i < 5; i++) {
            if (want[i] == stickKeys[i])
                continue;
            stickKeys[i] = want[i];
            if (want[i])
                input.OnKeyDown(kKeys[i]);
            else
                input.OnKeyUp(kKeys[i]);
        }
    }

    void TouchControls::ReleaseAll(Input& input) {
        stickActive = false;
        knobX = knobY = 0.0f;
        ApplyStick(input);
        for (int i = 0; i < ButtonCount; i++)
            if (down[i] && !layout[i].toggle)
                ReleaseButton(input, i);
        for (Finger& f : fingers) {
            if (f.role == Role::Pointer)
                input.OnMouseUp(Mouse::Left);
            if (f.holding)
                input.OnMouseUp(Mouse::Left);
        }
        if (sneakLatched) {
            sneakLatched = false;
            down[Sneak] = false;
            input.OnKeyUp(Key::Shift);
        }
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
        f.since = Clock::now();
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
        const int hit = HitButton(x, y);
        if (hit >= 0) {
            f.role = Role::Button;
            f.button = hit;
            PressButton(input, hit);
        } else if (!stickActive && x < static_cast<float>(width) * 0.42f && y > static_cast<float>(height) * 0.25f) {
            f.role = Role::Stick;
            stickActive = true;
            stickId = id;
            stickX = x;
            stickY = y;
            knobX = knobY = 0.0f;
        } else {
            f.role = Role::Look;
        }
        fingers.push_back(f);
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
            input.OnMouseMove(x, y);
            break;
        case Role::Scroll:
            scrollAccum += dy;
            while (scrollAccum >= 40.0f) {
                input.OnMouseWheel(-1.0f);
                scrollAccum -= 40.0f;
            }
            while (scrollAccum <= -40.0f) {
                input.OnMouseWheel(1.0f);
                scrollAccum += 40.0f;
            }
            break;
        case Role::Stick: {
            const float radius = kStickRadius * static_cast<float>(std::min(width, height));
            float ox = x - stickX, oy = y - stickY;
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
        case Role::Look:
            input.OnMouseDelta(dx * kLookScale, dy * kLookScale);
            break;
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
            input.OnMouseUp(Mouse::Left);
            break;
        case Role::Stick:
            stickActive = false;
            knobX = knobY = 0.0f;
            ApplyStick(input);
            break;
        case Role::Button:
            ReleaseButton(input, f->button);
            break;
        case Role::Look: {
            if (f->holding) {
                input.OnMouseUp(Mouse::Left);
            } else if (!f->moved && std::chrono::duration<double>(Clock::now() - f->since).count() < kTapSeconds) {
                input.OnMouseDown(Mouse::Right);
                input.OnMouseUp(Mouse::Right);
            }
            break;
        }
        default:
            break;
        }
        Erase(id);
    }

    void TouchControls::Update(Input& input) {
        std::lock_guard lock(mutex);
        const bool gameplay = Gameplay(input);
        if (wasGameplay != gameplay) {
            wasGameplay = gameplay;
            ReleaseAll(input);
            return;
        }
        if (!gameplay)
            return;
        const auto now = Clock::now();
        for (Finger& f : fingers) {
            if (f.role != Role::Look || f.holding || f.moved)
                continue;
            if (std::chrono::duration<double>(now - f.since).count() >= kHoldSeconds) {
                f.holding = true;
                input.OnMouseDown(Mouse::Left);
            }
        }
    }

    void TouchControls::Draw(RenderContext& renderer, bool visible) {
        std::lock_guard lock(mutex);
        if (!visible || width <= 0 || height <= 0)
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

        const float u = static_cast<float>(std::min(width, height));
        Mesh discs, text;

        if (stickActive) {
            const float radius = kStickRadius * u;
            Disc(discs, stickX, stickY, radius, Pack(1, 1, 1, 0.16f), false);
            Disc(discs, stickX, stickY, radius, Pack(1, 1, 1, 0.55f), true);
            Disc(discs, stickX + knobX, stickY + knobY, radius * 0.42f, Pack(1, 1, 1, 0.45f), false);
        } else {
            const float radius = kStickRadius * u;
            const float cx = radius * 1.4f, cy = static_cast<float>(height) - radius * 1.5f;
            Disc(discs, cx, cy, radius, Pack(1, 1, 1, 0.08f), false);
            Disc(discs, cx, cy, radius, Pack(1, 1, 1, 0.28f), true);
        }

        for (int i = 0; i < ButtonCount; i++) {
            const Layout& l = layout[i];
            const bool active = down[i];
            Disc(discs, l.cx, l.cy, l.r, Pack(active ? 1.0f : 0.1f, active ? 1.0f : 0.1f, active ? 1.0f : 0.1f, active ? 0.55f : 0.32f), false);
            Disc(discs, l.cx, l.cy, l.r, Pack(1, 1, 1, active ? 0.9f : 0.55f), true);

            const std::string label = l.label;
            const float scale = std::max(1.0f, std::floor(l.r / 9.0f));
            float total = 0.0f;
            size_t k = 0;
            uint32_t cp = 0;
            std::vector<uint32_t> cps;
            while (k < label.size() && ui::McFont::Next(label, k, cp)) {
                cps.push_back(cp);
                total += static_cast<float>(font.Get(cp).advance) * scale;
            }
            float pen = l.cx - total * 0.5f;
            const float top = l.cy - 4.0f * scale;
            for (uint32_t c : cps) {
                const ui::McFont::Glyph& g = font.Get(c);
                if (g.width > 0)
                    text.Quad(pen, top + g.top * scale, pen + g.width * scale, top + (g.top + g.height) * scale, g.u0, g.v0, g.u1, g.v1, Pack(1, 1, 1, 0.95f));
                pen += static_cast<float>(g.advance) * scale;
            }
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
        submit(text, fontTexture);
    }
}
