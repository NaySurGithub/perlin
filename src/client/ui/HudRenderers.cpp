#include "client/ui/HudRenderers.hpp"
#include "client/ui/UiExpr.hpp"
#include "client/ui/McFont.hpp"

#include <algorithm>
#include <cmath>

namespace perlin::ui {
    int JavaRandom::Next(int bits) {
        state = (state * 0x5DEECE66Dull + 0xBull) & kMask;
        return static_cast<int>(static_cast<int64_t>(state) >> (48 - bits));
    }

    int JavaRandom::NextInt(int bound) {
        if (bound <= 0)
            return 0;
        int r = Next(31);
        const int m = bound - 1;
        if ((bound & m) == 0)
            return static_cast<int>((static_cast<int64_t>(bound) * static_cast<int64_t>(r)) >> 31);
        for (int u = r; u - (r = u % bound) + m < 0; u = Next(31)) {
        }
        return r;
    }

    namespace {
        void Tex(RmlWriter& w, const LNode& n, float x, float y, float size, const char* texture, int zOffset = 0,
                 float alpha = 1.0f) {
            ImageStyle st;
            st.alpha = alpha * n.alpha;
            w.Image(x, y, size, size, n.layer + zOffset, std::string("textures/ui/") + texture, st);
        }

        struct HeartSet {
            const char* full;
            const char* half;
            const char* flashFull;
            const char* flashHalf;
        };

        HeartSet HeartsFor(const HudState& s) {
            if (s.wither) return {"wither_heart", "wither_heart_half", "wither_heart_flash", "wither_heart_flash_half"};
            if (s.poison) return {"poison_heart", "poison_heart_half", "poison_heart_flash", "poison_heart_flash_half"};
            if (s.freezing) return {"freeze_heart", "freeze_heart_half", "freeze_heart_flash", "freeze_heart_flash_half"};
            return {"heart", "heart_half", "heart_flash", "heart_flash_half"};
        }

        int HealthRows(const HudState& s) {
            const int hearts = static_cast<int>(std::ceil((s.maxHealth + std::ceil(s.absorption)) / 2.0f));
            return std::max(1, (hearts + 9) / 10);
        }

        int RowHeight(const HudState& s) { return std::max(10 - (HealthRows(s) - 2), 3); }

        void Hearts(const HudState& s, const LNode& n, RmlWriter& w) {
            const int health = static_cast<int>(std::ceil(s.health));
            const int shown = static_cast<int>(std::ceil(s.displayHealth));
            const int maxHealth = static_cast<int>(std::ceil(s.maxHealth));
            int absorption = static_cast<int>(std::ceil(s.absorption));
            const int rows = HealthRows(s);
            const int rowHeight = RowHeight(s);
            const bool blinking = s.healthBlinkUntil > static_cast<int>(s.tick) && ((s.healthBlinkUntil - static_cast<int>(s.tick)) / 3) % 2 == 1;
            const int regen = s.regeneration ? static_cast<int>(s.tick % static_cast<uint64_t>(std::ceil(s.maxHealth + 5.0f))) : -1;
            JavaRandom random(static_cast<int64_t>(s.tick) * 312871);
            const HeartSet set = HeartsFor(s);
            const int total = static_cast<int>(std::ceil((s.maxHealth + std::ceil(s.absorption)) / 2.0f));
            const int normalHearts = static_cast<int>(std::ceil(s.maxHealth / 2.0));
            int absorptionLeft = absorption;
            (void)rows;
            for (int i = total - 1; i >= 0; i--) {
                const int row = i / 10, col = i % 10;
                const float x = n.ax + col * 8;
                float y = n.ay - row * rowHeight;
                if (health + absorption <= 4)
                    y += random.NextInt(2);
                if (i < normalHearts && i == regen)
                    y -= 2;
                Tex(w, n, x, y, 9, blinking ? "heart_blink" : "heart_background", 0);
                const int twice = i * 2;
                if (i >= normalHearts) {
                    const int index = twice - maxHealth;
                    if (index < absorption) {
                        const bool half = index + 1 == absorption;
                        Tex(w, n, x, y, 9, half ? "absorption_heart_half" : "absorption_heart", 1);
                        (void)absorptionLeft;
                    }
                    continue;
                }
                if (blinking && twice < shown) {
                    const bool half = twice + 1 == shown;
                    Tex(w, n, x, y, 9, half ? set.flashHalf : set.flashFull, 1);
                }
                if (twice < health) {
                    const bool half = twice + 1 == health;
                    Tex(w, n, x, y, 9, half ? set.half : set.full, 2);
                }
            }
            if (s.hardcore) {
            }
        }

        void Armor(const HudState& s, const LNode& n, RmlWriter& w) {
            const int rows = HealthRows(s);
            const float y = n.ay - 10 - (rows - 1) * RowHeight(s);
            for (int i = 0; i < 10; i++) {
                const float x = n.ax + i * 8;
                const char* tex = i * 2 + 1 < s.armor ? "armor_full" : (i * 2 + 1 == s.armor ? "armor_half" : "armor_empty");
                Tex(w, n, x, y, 9, tex);
            }
        }

        void Hunger(const HudState& s, const LNode& n, RmlWriter& w) {
            JavaRandom random(static_cast<int64_t>(s.tick) * 312871);
            const bool effect = s.hungerEffect;
            const float right = n.ax + 1;
            for (int i = 0; i < 10; i++) {
                const float x = right - i * 8 - 9;
                float y = n.ay;
                if (s.saturation <= 0.0f && s.tick % static_cast<uint64_t>(s.food * 3 + 1) == 0)
                    y += random.NextInt(3) - 1;
                Tex(w, n, x, y, 9, effect ? "hunger_effect_background" : "hunger_background", 0);
                const char* tex = nullptr;
                if (i * 2 + 1 < s.food) tex = effect ? "hunger_effect_full" : "hunger_full";
                else if (i * 2 + 1 == s.food) tex = effect ? "hunger_effect_half" : "hunger_half";
                if (tex)
                    Tex(w, n, x, y, 9, tex, 1);
            }
        }

        void Bubbles(const HudState& s, const LNode& n, RmlWriter& w) {
            if (!s.Survival() || !(s.inWater || s.air < s.maxAir))
                return;
            const int air = std::max(0, s.air);
            const int full = static_cast<int>(std::ceil((air - 2) * 10.0 / s.maxAir));
            const int partial = static_cast<int>(std::ceil(air * 10.0 / s.maxAir)) - full;
            const float right = n.ax + 1;
            for (int i = 0; i < full + partial; i++)
                Tex(w, n, right - i * 8 - 9, n.ay, 9, i < full ? "bubble" : "bubble_pop");
        }

        void Hotbar(const HudState&, const LNode& n, RmlWriter& w) {
            if (n.index < 0 || n.index > 8)
                return;
            ImageStyle st;
            st.alpha = n.alpha;
            w.Image(n.ax, n.ay, n.w, n.h, n.layer, "textures/ui/hotbar_" + std::to_string(n.index), st);
        }

        void InventoryItem(const HudState& s, const LNode& n, RmlWriter& w) {
            if (n.index < 0 || n.index > 8)
                return;
            const HudItem& item = s.hotbar[n.index];
            if (item.Empty())
                return;
            w.Item(n.ax, n.ay, n.w, n.h, n.layer, item.Key(), n.alpha, item.enchanted);
        }

        void ProgressBar(const HudState& s, const LNode& n, RmlWriter& w) {
            const Json* bag = n.Get("property_bag");
            const bool durability = bag && bag->Find("is_durability") && bag->Find("is_durability")->BoolOr(false);
            if (!durability)
                return;
            const Json* vis = n.bag.Find("#touch_progress_bar_visible");
            if (!vis || !Truthy(*vis))
                return;
            const Json* total = n.bag.Find("#progress_bar_total_amount");
            const Json* current = n.bag.Find("#progress_bar_current_amount");
            if (!total || !current || total->number <= 0)
                return;
            (void)s;
            const double max = total->number;
            const double damage = max - current->number;
            const double ratio = std::max(0.0, (max - damage) / max);
            const int width = static_cast<int>(std::lround(13.0 - damage * 13.0 / max));
            const double h = ratio / 3.0 * 6.0;
            const double f = h - std::floor(h);
            double r = 0, g = 0, b = 0;
            switch (static_cast<int>(h) % 6) {
            case 0: r = 1; g = f; b = 0; break;
            case 1: r = 1 - f; g = 1; b = 0; break;
            case 2: r = 0; g = 1; b = f; break;
            default: r = 0; g = 1; b = 1; break;
            }
            w.Rect(n.ax, n.ay, 13, 2, n.layer, 0, 0, 0, 1);
            w.Rect(n.ax, n.ay, static_cast<float>(width), 1, n.layer + 1, static_cast<float>(r), static_cast<float>(g),
                   static_cast<float>(b), 1);
        }
    }

    namespace {
        void Debug(const HudState& s, const LNode& n, RmlWriter& w) {
            if (!s.debug)
                return;
            const McFont& font = McFont::Instance();
            TextStyle st;
            st.r = st.g = st.b = 224.0f / 255.0f;
            const float bg = 0x50 / 255.0f, bga = 0x90 / 255.0f;
            const auto column = [&](const std::vector<std::string>& lines, bool right) {
                for (size_t i = 0; i < lines.size(); i++) {
                    if (lines[i].empty())
                        continue;
                    const float width = font.Width(lines[i]);
                    const float y = 2.0f + 9.0f * static_cast<float>(i);
                    const float x = right ? s.guiW - 2.0f - width : 2.0f;
                    w.Rect(x - 1, y - 1, width + 1, 9, n.layer, bg, bg, bg, bga);
                    w.Text(x, y, width + 1, 9, n.layer + 1, {lines[i]}, st);
                }
            };
            column(s.debugLeft, false);
            column(s.debugRight, true);
        }

        void Subtitles(const HudState& s, const LNode& n, RmlWriter& w) {
            if (s.subtitles.empty())
                return;
            const McFont& font = McFont::Instance();
            const float arrowWidth = font.Width("<") + font.Width(" ") + font.Width(">") + font.Width(" ");
            float widest = 0.0f;
            for (const HudState::SubtitleLine& l : s.subtitles)
                widest = std::max(widest, font.Width(l.text));
            const float inner = widest + arrowWidth;
            const float boxW = inner + 4.0f;
            const float left = s.guiW - boxW - 2.0f;
            float y = s.guiH - 30.0f - 10.0f * static_cast<float>(s.subtitles.size());
            for (const HudState::SubtitleLine& l : s.subtitles) {
                w.Rect(left, y, boxW, 10.0f, n.layer, 0, 0, 0, 0.8f);
                TextStyle st;
                st.shadow = false;
                st.r = st.g = st.b = l.brightness;
                const float textW = font.Width(l.text);
                w.Text(left + 2.0f + (inner - textW) * 0.5f, y + 1.0f, textW + 2.0f, 9, n.layer + 1, {l.text}, st);
                if (l.direction < 0)
                    w.Text(left + 2.0f, y + 1.0f, 8, 9, n.layer + 1, {"<"}, st);
                else if (l.direction > 0)
                    w.Text(left + boxW - 2.0f - font.Width(">"), y + 1.0f, 8, 9, n.layer + 1, {">"}, st);
                y += 10.0f;
            }
        }

        void TabList(const HudState& s, const LNode& n, RmlWriter& w) {
            if (!s.tabOpen || s.tabEntries.empty())
                return;
            const McFont& font = McFont::Instance();
            const int count = std::min<int>(static_cast<int>(s.tabEntries.size()), 80);
            int columns = 1, rows = count;
            while (rows > 20) {
                columns++;
                rows = (count + columns - 1) / columns;
            }
            float widest = 0.0f;
            for (int i = 0; i < count; i++)
                widest = std::max(widest, font.Width(s.tabEntries[static_cast<size_t>(i)].name));
            const float maxColumn = (s.guiW - 50.0f) / static_cast<float>(columns);
            const float cell = std::min(9.0f + widest + 4.0f, maxColumn);
            const float total = cell * static_cast<float>(columns) + static_cast<float>(columns - 1) * 1.0f;
            const float x0 = std::floor((s.guiW - total) * 0.5f);
            const float y0 = 10.0f;
            const int z = n.layer + 6;
            w.Rect(x0 - 1, y0 - 1, total + 2, static_cast<float>(rows) * 9.0f + 1.0f, z, 0, 0, 0, 0.5f);
            for (int i = 0; i < count; i++) {
                const int col = i / rows, row = i % rows;
                const float x = x0 + static_cast<float>(col) * (cell + 1.0f), y = y0 + static_cast<float>(row) * 9.0f;
                w.Rect(x, y, cell, 8.0f, z + 1, 1, 1, 1, 0.125f);
                const HudState::TabEntry& e = s.tabEntries[static_cast<size_t>(i)];
                ImageStyle st;
                w.Image(x, y, 8, 8, z + 2, "mchead:" + e.headKey, st);
                TextStyle ts;
                ts.shadow = true;
                ts.r = ts.g = ts.b = 1.0f;
                w.Text(x + 9.0f, y, cell - 9.0f - 1.0f, 9, z + 2, {e.name}, ts);
            }
        }

        void Chat(const HudState& s, const LNode& n, RmlWriter& w) {
            TabList(s, n, w);
            const McFont& font = McFont::Instance();
            const float width = 320.0f;
            w.BeginSection();
            for (const HudState::ChatLine& l : s.chatLines) {
                if (l.alpha * 255.0f <= 3.0f)
                    continue;
                const float top = 171.0f - static_cast<float>(l.row) * 9.0f;
                if (top < 0.0f)
                    continue;
                w.Rect(0, top, width + 8, 9, 1, 0, 0, 0, 0.5f * l.alpha);
                for (const HudState::ChatLine::Click& c : l.clicks)
                    w.Link(4 + c.x, top, c.w, 9, 3, c.command);
                TextStyle st;
                st.shadow = true;
                st.a = l.alpha;
                w.Text(4, top + 1, width, 9, 2, {l.text}, st);
            }
            w.EndSection("chat_lines");
            w.BeginSection();
            if (s.chatScrolled && s.chatTotalLines > 0) {
                const float track = 180.0f;
                const float thumb = std::max(4.0f, track * 20.0f / static_cast<float>(std::max(20, s.chatTotalLines)));
                const float pos = (track - thumb) * static_cast<float>(s.chatScroll) /
                                  static_cast<float>(std::max(1, s.chatTotalLines - 20));
                w.Rect(0, 180.0f - pos - thumb, 2, thumb, 1, 0.8f, 0.8f, 0.8f, 0.9f);
            }
            w.EndSection("chat_scroll");
            w.BeginSection();
            if (s.chatOpen) {
                const float barW = s.guiW - 4.0f;
                w.Rect(0, 0, barW, 12, 1, 0, 0, 0, 0.5f);
                const std::string& in = s.chatInput;
                const size_t caret = std::min(static_cast<size_t>(std::max(0, s.chatCaret)), in.size());
                const float room = s.guiW - 12.0f;
                size_t start = 0;
                while (start < caret && font.Width(in.substr(start, caret - start)) > room - 6) {
                    start++;
                    while (start < caret && (static_cast<unsigned char>(in[start]) & 0xC0) == 0x80)
                        start++;
                }
                const std::string before = in.substr(start, caret - start);
                const std::string after = in.substr(caret);
                TextStyle st;
                st.shadow = true;
                st.r = st.g = st.b = 224.0f / 255.0f;
                w.Text(2, 2, room, 9, 2, {before + after}, st);
                const float cx = 2.0f + font.Width(before);
                if (s.chatCaretVisible) {
                    if (caret >= in.size())
                        w.Text(cx, 2, 8, 9, 3, {"_"}, st);
                    else
                        w.Rect(cx, 1, 1, 10, 3, 0.82f, 0.82f, 0.82f, 1.0f);
                }
            }
            if (s.chatOpen && !s.chatGhost.empty()) {
                TextStyle gs;
                gs.shadow = true;
                gs.r = gs.g = gs.b = 128.0f / 255.0f;
                const std::string& in = s.chatInput;
                w.Text(2.0f + font.Width(in), 2, 200, 9, 2, {s.chatGhost}, gs);
            }
            if (s.chatOpen && !s.chatSuggestions.empty()) {
                float boxW = 0;
                for (const std::string& g : s.chatSuggestions)
                    boxW = std::max(boxW, font.Width(g));
                float x = 2.0f + font.Width(s.chatInput.substr(0, std::min(static_cast<size_t>(s.chatSuggestStart), s.chatInput.size())));
                x = std::clamp(x, 0.0f, std::max(0.0f, s.guiW - 4.0f - boxW - 1.0f));
                const float rows = static_cast<float>(s.chatSuggestions.size());
                w.Rect(x - 1.0f, -rows * 12.0f - 2.0f, boxW + 3.0f, rows * 12.0f + 2.0f, 4, 0, 0, 0, 208.0f / 255.0f);
                for (size_t i = 0; i < s.chatSuggestions.size(); i++) {
                    TextStyle ts;
                    ts.shadow = true;
                    const bool sel = static_cast<int>(i) == s.chatSuggestSelected;
                    if (sel) { ts.r = 1.0f; ts.g = 1.0f; ts.b = 0.0f; }
                    else ts.r = ts.g = ts.b = 170.0f / 255.0f;
                    w.Text(x + 1.0f - 1.0f + 0.0f, -rows * 12.0f + 1.0f + static_cast<float>(i) * 12.0f, boxW + 1.0f, 9, 5, {s.chatSuggestions[i]}, ts);
                }
                const auto dots = [&](float y) {
                    for (float dx = 0; dx < boxW + 1.0f; dx += 2.0f)
                        w.Rect(x + dx, y, 1, 1, 6, 1, 1, 1, 1);
                };
                if (s.chatSuggestMore[0])
                    dots(-rows * 12.0f - 1.0f);
                if (s.chatSuggestMore[1])
                    dots(-1.0f);
            } else if (s.chatOpen && !s.chatHint.empty()) {
                TextStyle ts;
                ts.shadow = true;
                ts.r = ts.g = ts.b = 119.0f / 255.0f;
                w.Text(2.0f + font.Width(s.chatInput) + font.Width(" "), 2, 160, 9, 2, {s.chatHint}, ts);
            }
            w.EndSection("chat_input");
        }
    }

    void RegisterHudRenderers(RmlEmitter& emitter, const HudState& state, const UiServices&) {
        const HudState* s = &state;
        emitter.Register("heart_renderer", [s](const LNode& n, RmlWriter& w) { Hearts(*s, n, w); });
        emitter.Register("armor_renderer", [s](const LNode& n, RmlWriter& w) { Armor(*s, n, w); });
        emitter.Register("hunger_renderer", [s](const LNode& n, RmlWriter& w) { Hunger(*s, n, w); });
        emitter.Register("bubbles_renderer", [s](const LNode& n, RmlWriter& w) { Bubbles(*s, n, w); });
        emitter.Register("hotbar_renderer", [s](const LNode& n, RmlWriter& w) { Hotbar(*s, n, w); });
        emitter.Register("inventory_item_renderer", [s](const LNode& n, RmlWriter& w) { InventoryItem(*s, n, w); });
        emitter.Register("debug_renderer", [s](const LNode& n, RmlWriter& w) { Debug(*s, n, w); });
        emitter.Register("chat_renderer", [s](const LNode& n, RmlWriter& w) { Chat(*s, n, w); });
        emitter.Register("subtitle_renderer", [s](const LNode& n, RmlWriter& w) { Subtitles(*s, n, w); });
        emitter.Register("progress_bar_renderer", [s](const LNode& n, RmlWriter& w) { ProgressBar(*s, n, w); });
        for (const char* name : {"horse_jump_renderer", "dash_renderer", "locator_bar", "mob_effects_renderer",
                                 "vignette_renderer", "camera_renderer", "hud_player_renderer", "horse_heart_renderer",
                                 "cooldown_renderer", "editor_gizmo_renderer", "editor_compass_renderer",
                                 "editor_volume_highlight_renderer", "progress_indicator_renderer"})
            emitter.Register(name, [](const LNode&, RmlWriter&) {});
    }
}
