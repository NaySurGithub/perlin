#include "client/ui/HudData.hpp"
#include "client/i18n/Lang.hpp"

#include <algorithm>
#include <cmath>

namespace perlin::ui {
    namespace {
        Json B(bool v) { return Json(v); }
        Json N(double v) { return Json(v); }
    }

    Vars EngineRootVariables() {
        Vars v;
        Json xp = Json::MakeArray();
        xp.Push(Json(0.5019608));
        xp.Push(Json(1.0));
        xp.Push(Json(0.1254902));
        v["$experience_text_color"] = xp;
        v["$desktop_screen"] = Json(true);
        v["$pocket_screen"] = Json(false);
        v["$console_screen"] = Json(false);
        v["$gamepad_screen"] = Json(false);
        v["$touch"] = Json(false);
        v["$education_edition"] = Json(false);
        v["$compress_hud_width"] = Json(false);
        v["$is_editor_mode_enabled"] = Json(false);
        v["$is_holographic"] = Json(false);
        v["$is_vr"] = Json(false);
        v["$splitscreen"] = Json(false);
        v["$ps4"] = Json(false);
        v["$xbox"] = Json(false);
        v["$switch"] = Json(false);
        return v;
    }

    bool HudData::Global(const std::string& name, Json& out) const {
        const bool survival = s.Survival();
        const bool spectator = s.mode == GameMode::Spectator;
        const bool hud = s.visible;
        const auto set = [&](Json v) { out = std::move(v); return true; };

        if (name == "#hud_visible") return set(B(hud));
        if (name == "#hud_visible_centered" || name == "#hud_visible_centered_gui_elements") return set(B(hud));
        if (name == "#hud_visible_centered_touch" || name == "#hud_visible_not_centered" ||
            name == "#hotbar_visible_not_centered" || name == "#hotbar_visible_not_centered_resizable")
            return set(B(false));
        if (name == "#hud_alpha") return set(N(1.0));
        if (name == "#hud_propagate_alpha") return set(B(false));
        if (name == "#hotbar_visible") return set(B(hud));
        if (name == "#hotbar_with_xp_bar") return set(B(hud && survival && !s.locatorActive));
        if (name == "#hotbar_no_xp_bar") return set(B(hud && !survival && !spectator && !s.locatorActive));
        if (name == "#hotbar_with_locator_bar") return set(B(hud && !spectator && s.locatorActive));
        if (name == "#show_survival_ui") return set(B(hud && survival));
        if (name == "#is_armor_visible") return set(B(hud && survival && s.armor > 0));
        if (name == "#is_not_riding_bubbles") return set(B(true));
        if (name == "#is_riding_bubbles" || name == "#survival_horse_hearts" || name == "#creative_horse_hearts" ||
            name == "#horse_hearts_touch")
            return set(B(false));
        if (name == "#is_spectator_mode") return set(B(spectator));
        if (name == "#paper_doll_visible") return set(B(false));
        if (name == "#exp_progress") return set(N(std::clamp(s.xpProgress, 0.0f, 1.0f)));
        if (name == "#level_number") return set(Json(std::to_string(s.xpLevel)));
        if (name == "#level_number_visible") return set(B(survival && s.xpLevel > 0));
        if (name == "#hotbar_grid_dimensions") {
            Json a = Json::MakeArray();
            a.Push(N(9));
            a.Push(N(1));
            return set(std::move(a));
        }
        if (name == "#item_text") return set(Json(s.itemName));
        if (name == "#item_name_alpha") return set(N(s.itemNameAlpha));
        if (name == "#actionbar_alpha") return set(N(s.actionBarAlpha));
        if (name == "#title_alpha") return set(N(s.titleAlpha));
        if (name == "#hud_title_text_string") return set(Json(s.title));
        if (name == "#hud_subtitle_text_string") return set(Json(s.subtitle));
        if (name == "#tooltip_visible") return set(B(!s.itemName.empty()));
        if (name == "#status_effects_visible") return set(B(hud && !s.effects.empty()));
        if (name == "#scoreboard_sidebar_visible") return set(B(false));
        if (name == "#interact_visible") return set(B(false));
        if (name == "#hud_text_background_alpha") return set(N(0.5));
        return false;
    }

    int HudData::CollectionSize(const std::string& collection) const {
        return collection == "hotbar_items" ? 9 : 0;
    }

    bool HudData::CollectionValue(const std::string& collection, int index, const std::string& binding, Json& out) const {
        if (collection != "hotbar_items" || index < 0 || index >= 9)
            return false;
        const HudItem& item = s.hotbar[index];
        const auto set = [&](Json v) { out = std::move(v); return true; };
        if (binding == "#slot_selected") return set(B(index == s.selectedSlot));
        if (binding == "#hotbar_visible") return set(B(s.visible));
        if (binding == "#item_id_aux") return set(N(item.Empty() ? 0 : 1));
        if (binding == "#inventory_stack_count") return set(Json(!item.Empty() && item.count > 1 ? std::to_string(item.count) : std::string()));
        if (binding == "#stack_count_visible") return set(B(!item.Empty() && item.count > 1));
        if (binding == "#item_durability_visible") return set(B(!item.Empty() && item.maxDamage > 0 && item.aux > 0));
        if (binding == "#item_durability_total_amount") return set(N(item.maxDamage));
        if (binding == "#item_durability_current_amount") return set(N(std::max(0, item.maxDamage - item.aux)));
        if (binding == "#item_storage_visible") return set(B(false));
        if (binding == "#item_storage_total_amount" || binding == "#item_storage_current_amount") return set(N(0));
        if (binding == "#inactive_item") return set(B(false));
        if (binding == "#item_renderer_data") return set(N(item.Empty() ? 0 : 1));
        return false;
    }

    std::vector<FactoryItem> HudData::Factory(const std::string& name) const {
        std::vector<FactoryItem> items;
        if (name == "item_text_factory") {
            if (s.visible && !s.itemName.empty() && s.itemNameAlpha > 0.0f)
                items.push_back({"item_text", {}});
        } else if (name == "hud_actionbar_text_factory") {
            if (s.visible && !s.actionBar.empty() && s.actionBarAlpha > 0.0f) {
                FactoryItem f{"hud_actionbar_text", {}};
                f.vars["$actionbar_text"] = Json(s.actionBar);
                items.push_back(std::move(f));
            }
        } else if (name == "hud_title_text_factory") {
            if (s.visible && !s.title.empty() && s.titleAlpha > 0.0f) {
                FactoryItem f{"hud_title_text", {}};
                f.vars["$title_alpha"] = Json(0.6);
                items.push_back(std::move(f));
            }
        }
        return items;
    }

    uint64_t HudSignature(const HudState& s) {
        uint64_t h = 1469598103934665603ull;
        const auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
        const auto mixf = [&](float v) { mix(static_cast<uint64_t>(static_cast<int64_t>(std::lround(v * 256.0f)))); };
        const auto mixs = [&](const std::string& t) { for (const char c : t) mix(static_cast<uint8_t>(c)); mix(0xff); };
        mix(i18n::Lang::Get().Version());
        mix(s.visible);
        mix(static_cast<uint64_t>(s.mode));
        mixf(s.xpProgress);
        mix(s.locatorActive);
        for (const HudEffect& e : s.effects) {
            mix(static_cast<uint64_t>(e.id));
            mix(static_cast<uint64_t>(e.amplifier));
            mix(static_cast<uint64_t>(e.ticks < 0 ? 999999 : e.ticks / 20));
            mix(e.ambient);
        }
        for (const LocatorMarker& m : s.locator) {
            mixf(m.angle * 4.0f);
            mix(static_cast<uint64_t>(m.pitch + 2));
            mix(m.color);
            mix(static_cast<uint64_t>(std::lround(m.distance / 8.0f)));
        }
        mix(static_cast<uint64_t>(s.xpLevel));
        mix(static_cast<uint64_t>(s.armor));
        mix(static_cast<uint64_t>(s.selectedSlot));
        for (const HudItem& it : s.hotbar) {
            mixs(it.name);
            mix(static_cast<uint64_t>(it.count));
            mix(static_cast<uint64_t>(it.aux));
            mix(it.blockRuntimeId);
            mix(it.enchanted);
            mixs(it.customName);
        }
        mixs(s.itemName);
        mix(s.burning);
        if (s.burning)
            mix(static_cast<uint64_t>(s.fireFrame));
        mixf(s.itemNameAlpha);
        mixs(s.actionBar);
        mixf(s.actionBarAlpha);
        mixs(s.title);
        mixs(s.subtitle);
        mixf(s.titleAlpha);
        for (const HudState::SubtitleLine& l : s.subtitles) {
            mixs(l.text);
            mix(static_cast<uint64_t>(l.direction + 2));
            mixf(l.brightness);
        }
        const bool animated = s.health + s.absorption <= 4.0f || s.regeneration || s.healthBlinkUntil > static_cast<int>(s.tick) ||
                              s.saturation <= 0.0f;
        if (animated)
            mix(s.tick);
        mixf(s.health);
        mixf(s.displayHealth);
        mixf(s.absorption);
        mixf(s.maxHealth);
        mix((s.poison ? 1u : 0u) | (s.wither ? 2u : 0u) | (s.freezing ? 4u : 0u) | (s.hungerEffect ? 8u : 0u) |
            (s.inWater ? 16u : 0u) | (s.regeneration ? 32u : 0u));
        mix(static_cast<uint64_t>(s.food));
        mix(static_cast<uint64_t>(s.air));
        mixf(s.guiW);
        mixf(s.guiH);
        mix(s.chatOpen);
        mix(s.tabOpen);
        if (s.tabOpen)
            for (const auto& t : s.tabEntries) {
                mixs(t.name);
                mixs(t.headKey);
            }
        mix(s.debug);
        if (s.debug) {
            for (const std::string& l : s.debugLeft) mixs(l);
            for (const std::string& l : s.debugRight) mixs(l);
        }
        for (const auto& l : s.chatLines) {
            mixs(l.text);
            mixf(l.alpha);
            mix(static_cast<uint64_t>(l.row));
        }
        if (s.chatOpen) {
            mixs(s.chatInput);
            mix(static_cast<uint64_t>(s.chatCaret));
            mix(s.chatCaretVisible);
            for (const std::string& g : s.chatSuggestions)
                mixs(g);
            mix(static_cast<uint64_t>(s.chatSuggestSelected + 1));
            mix(static_cast<uint64_t>(s.chatSuggestStart));
            mixs(s.chatHint);
            mixs(s.chatGhost);
            mix(static_cast<uint64_t>(s.chatSuggestMore[0]) | (static_cast<uint64_t>(s.chatSuggestMore[1]) << 1));
            mix(static_cast<uint64_t>(s.chatScroll));
            mix(static_cast<uint64_t>(s.chatTotalLines));
        }
        return h;
    }
}
