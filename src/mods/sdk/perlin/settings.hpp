#pragma once
// Mod settings.
//
// Settings are declared in the mod's JSON (see mods/README.md); the game shows them on the mod's Settings page,
// stores them, and keeps them up to date here. The build turns the JSON into a header of typed handles:
//
//     #include <mod_settings.hpp>              // generated from waila.json
//
//     if (settings::visible) { ... }                       // Toggle   -> bool
//     const double limit = settings::warn_at;              // Number   -> double
//     if (settings::unit == settings::Unit::Metric) { }    // Choice   -> an enum made from the JSON's choices
//     settings::visible.set(false);
//
//     ctx.on_change(settings::unit, [](settings::Unit u) { ... });
//     ctx.on_press(settings::reset, [] { ... });
//
// Without the generated header the same values are one call away: `perlin::setting<double>("warn_at")`.
#include <charconv>
#include <format>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "perlin/rpc.hpp"

namespace perlin {
    namespace detail {
        // std::from_chars for floating point is missing from some standard libraries (wasi-sdk's libc++), so numbers
        // go through strtod. Returns whether the whole text was a number.
        template <std::floating_point T>
        bool parse_floating(std::string_view text, T& out) {
            if (text.empty() || text.size() > 64)
                return false;
            const std::string copy(text);
            char* end = nullptr;
            const double value = std::strtod(copy.c_str(), &end);
            if (end != copy.c_str() + copy.size())
                return false;
            out = static_cast<T>(value);
            return true;
        }
    }

    namespace detail_settings {
        // Every setting's current value as text, loaded from the game on first use and kept current by SettingChanged.
        class Store {
        public:
            const std::string* find(std::string_view id) {
                load();
                const auto it = values.find(id);
                return it == values.end() ? nullptr : &it->second;
            }
            void put(std::string_view id, std::string value) {
                load();
                values[std::string(id)] = std::move(value);
            }

        private:
            void load() {
                if (loaded)
                    return;
                loaded = true;
                if (auto list = detail::call_list<abi::SettingEntry>(Op::SettingList))
                    for (auto& entry : *list)
                        values[std::move(entry.id)] = std::move(entry.value);
            }
            std::map<std::string, std::string, std::less<>> values;
            bool loaded = false;
        };

        inline Store& store() {
            static Store instance;
            return instance;
        }

        template <class T>
        T parse(std::string_view text) {
            if constexpr (std::is_same_v<T, bool>) {
                return text == "true" || text == "1";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return std::string(text);
            } else if constexpr (std::is_enum_v<T>) {
                return meta::enum_cast_nocase<T>(text).value_or(T{});
            } else if constexpr (std::is_floating_point_v<T>) {
                T value{};
                (void)detail::parse_floating(text, value);
                return value;
            } else {
                static_assert(std::is_integral_v<T>, "a setting is a bool, a number, a string or an enumeration");
                T value{};
                (void)std::from_chars(text.data(), text.data() + text.size(), value);
                return value;
            }
        }

        template <class T>
        std::string format(const T& value) {
            if constexpr (std::is_same_v<T, bool>) {
                return value ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return value;
            } else if constexpr (std::is_enum_v<T>) {
                return std::string(meta::enum_name(value));
            } else {
                return std::format("{}", value);
            }
        }
    }

    // A setting's value as the game stores it, straight from the game (not cached): "true", "12.5", "KilometersPerHour".
    inline Result<std::string> setting_text(std::string_view id) {
        abi::Writer w;
        w.write(id);
        auto bytes = detail::call(Op::SettingGet, w);
        if (!bytes)
            return unexpected(bytes.error());
        return std::string(bytes->begin(), bytes->end());
    }

    // The current value of a setting declared in the manifest, by id.
    template <class T>
    T setting(std::string_view id) {
        const std::string* text = detail_settings::store().find(id);
        return text ? detail_settings::parse<T>(*text) : T{};
    }

    // A typed handle to one setting. Reads always see the current value, including changes made on the settings page.
    template <class T>
    class Setting {
    public:
        using Value = T;
        constexpr explicit Setting(std::string_view setting_id) : name(setting_id) {}

        T get() const { return setting<T>(name); }
        operator T() const { return get(); }
        T operator*() const { return get(); }
        constexpr std::string_view id() const { return name; }

        // Changes the value from the mod; the game stores it and the settings page shows it.
        Result<void> set(const T& value) const {
            abi::Writer w;
            w.write(name, detail_settings::format(value));
            auto r = detail::call_void(Op::SettingSet, w);
            if (r)
                detail_settings::store().put(name, detail_settings::format(value));
            return r;
        }

        friend bool operator==(const Setting& s, const T& other)
            requires std::equality_comparable<T>
        {
            return s.get() == other;
        }

    private:
        std::string_view name;
    };

    using Toggle = Setting<bool>;
    using Integer = Setting<int>;
    using Number = Setting<double>;
    using Text = Setting<std::string>;
    template <meta::Enumeration E>
    using Choice = Setting<E>;

    // A button on the settings page. Handle presses with `ctx.on_press`.
    class Button {
    public:
        constexpr explicit Button(std::string_view button_id) : name(button_id) {}
        constexpr std::string_view id() const { return name; }

    private:
        std::string_view name;
    };
}
