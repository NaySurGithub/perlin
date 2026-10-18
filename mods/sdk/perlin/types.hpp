#pragma once
#include <cmath>
#include <compare>
#include <cstdint>
#include <format>
#include <numbers>
#include <string>
#include <string_view>

#include "perlin/abi.hpp"

namespace perlin {
    struct Vec3 {
        double x = 0, y = 0, z = 0;

        constexpr Vec3() = default;
        constexpr Vec3(double ax, double ay, double az) : x(ax), y(ay), z(az) {}

        constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
        constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
        constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
        constexpr Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
        constexpr Vec3 operator-() const { return {-x, -y, -z}; }
        constexpr Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
        constexpr Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
        constexpr bool operator==(const Vec3&) const = default;

        constexpr double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
        constexpr double length_squared() const { return dot(*this); }
        double length() const { return std::sqrt(length_squared()); }
        double horizontal_length() const { return std::sqrt(x * x + z * z); }
        double distance(const Vec3& o) const { return (*this - o).length(); }
        Vec3 normalized() const {
            const double l = length();
            return l > 0 ? *this / l : Vec3{};
        }
    };

    struct BlockPos {
        int x = 0, y = 0, z = 0;

        constexpr BlockPos() = default;
        constexpr BlockPos(int ax, int ay, int az) : x(ax), y(ay), z(az) {}
        explicit BlockPos(const Vec3& v) : x(static_cast<int>(std::floor(v.x))), y(static_cast<int>(std::floor(v.y))), z(static_cast<int>(std::floor(v.z))) {}

        constexpr auto operator<=>(const BlockPos&) const = default;
        constexpr BlockPos operator+(const BlockPos& o) const { return {x + o.x, y + o.y, z + o.z}; }
        constexpr BlockPos operator-(const BlockPos& o) const { return {x - o.x, y - o.y, z - o.z}; }
        constexpr BlockPos up(int n = 1) const { return {x, y + n, z}; }
        constexpr BlockPos down(int n = 1) const { return {x, y - n, z}; }
        constexpr Vec3 center() const { return {x + 0.5, y + 0.5, z + 0.5}; }
    };

    enum class Face : int { Down = 0, Up = 1, North = 2, South = 3, West = 4, East = 5 };

    enum class GameMode : int { Survival = 0, Creative = 1, Adventure = 2, Spectator = 6 };

    enum class Dimension : int { Overworld = 0, Nether = 1, End = 2 };

    struct Rgba {
        uint8_t r = 255, g = 255, b = 255, a = 255;

        constexpr Rgba() = default;
        constexpr Rgba(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255) : r(red), g(green), b(blue), a(alpha) {}
        static constexpr Rgba hex(uint32_t rgb, uint8_t alpha = 255) {
            return {static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8), static_cast<uint8_t>(rgb), alpha};
        }
        constexpr Rgba with_alpha(uint8_t alpha) const { return {r, g, b, alpha}; }
        constexpr uint32_t packed() const {
            return (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) | (static_cast<uint32_t>(b) << 8) | a;
        }

        static const Rgba white, black, red, green, blue, yellow, gray, dark_gray, gold, aqua, transparent;
    };
    inline constexpr Rgba Rgba::white{255, 255, 255};
    inline constexpr Rgba Rgba::black{0, 0, 0};
    inline constexpr Rgba Rgba::red{255, 85, 85};
    inline constexpr Rgba Rgba::green{85, 255, 85};
    inline constexpr Rgba Rgba::blue{85, 85, 255};
    inline constexpr Rgba Rgba::yellow{255, 255, 85};
    inline constexpr Rgba Rgba::gray{170, 170, 170};
    inline constexpr Rgba Rgba::dark_gray{85, 85, 85};
    inline constexpr Rgba Rgba::gold{255, 170, 0};
    inline constexpr Rgba Rgba::aqua{85, 255, 255};
    inline constexpr Rgba Rgba::transparent{0, 0, 0, 0};

    using Item = abi::Item;
    using Entity = abi::Entity;
    using PlayerState = abi::PlayerState;
    using Inventory = abi::Inventory;
    using WorldState = abi::WorldState;
    using BlockInfo = abi::BlockInfo;
    using RayHit = abi::RayHit;
    using AimState = abi::AimState;
    using ScreenState = abi::ScreenState;
    using Effect = abi::Effect;
    using PlayerEntry = abi::PlayerEntry;

    inline std::string_view short_name(std::string_view id) {
        const size_t colon = id.find(':');
        return colon == std::string_view::npos ? id : id.substr(colon + 1);
    }
}

template <>
struct std::formatter<perlin::Vec3> : std::formatter<double> {
    template <class FormatContext>
    auto format(const perlin::Vec3& v, FormatContext& ctx) const {
        auto out = ctx.out();
        out = std::format_to(out, "(");
        out = std::formatter<double>::format(v.x, ctx);
        out = std::format_to(out, ", ");
        out = std::formatter<double>::format(v.y, ctx);
        out = std::format_to(out, ", ");
        out = std::formatter<double>::format(v.z, ctx);
        return std::format_to(out, ")");
    }
};

template <>
struct std::formatter<perlin::BlockPos> : std::formatter<int> {
    template <class FormatContext>
    auto format(const perlin::BlockPos& p, FormatContext& ctx) const {
        return std::format_to(ctx.out(), "[{}, {}, {}]", p.x, p.y, p.z);
    }
};
