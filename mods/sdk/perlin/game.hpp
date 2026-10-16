#pragma once
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "perlin/rpc.hpp"
#include "perlin/types.hpp"

namespace perlin::game {
    inline Result<PlayerState> player() { return detail::call_as<PlayerState>(Op::PlayerState); }

    inline Vec3 position(const PlayerState& p) { return {p.x, p.y, p.z}; }
    inline Vec3 velocity(const PlayerState& p) { return {p.vx, p.vy, p.vz}; }
    inline Vec3 eye_position(const PlayerState& p) { return {p.x, p.y + p.eyeHeight, p.z}; }
    inline Vec3 look_direction(const PlayerState& p) {
        const double yaw = p.yaw * std::numbers::pi / 180.0, pitch = p.pitch * std::numbers::pi / 180.0;
        return {-std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    }

    inline Result<std::vector<Effect>> effects() {
        auto bytes = detail::call(Op::PlayerEffects);
        if (!bytes)
            return std::unexpected(bytes.error());
        abi::Reader r(bytes->data(), bytes->size());
        std::vector<Effect> out;
        const uint32_t n = r.get<uint32_t>();
        for (uint32_t i = 0; i < n && r.Ok(); i++)
            out.push_back(Effect::Read(r));
        return out;
    }

    inline Result<Inventory> inventory() { return detail::call_as<Inventory>(Op::Inventory); }

    inline Result<WorldState> world() { return detail::call_as<WorldState>(Op::WorldState); }

    inline Result<BlockInfo> block(const BlockPos& pos) {
        abi::Writer w;
        w.put(pos.x).put(pos.y).put(pos.z);
        return detail::call_as<BlockInfo>(Op::Block, w);
    }

    inline std::string block_name(const BlockPos& pos) {
        auto b = block(pos);
        return b && b->loaded ? b->name : std::string{};
    }

    inline bool chunk_loaded(int chunkX, int chunkZ) {
        abi::Writer w;
        w.put<int32_t>(chunkX).put<int32_t>(chunkZ);
        auto bytes = detail::call(Op::ChunkLoaded, w);
        return bytes && !bytes->empty() && (*bytes)[0] != 0;
    }

    inline Result<abi::Region> region(const BlockPos& from, const BlockPos& to) {
        abi::Writer w;
        w.put(from.x).put(from.y).put(from.z).put(to.x).put(to.y).put(to.z);
        return detail::call_as<abi::Region>(Op::Region, w);
    }

    struct RaycastOptions {
        double max_distance = 6.0;
        bool include_liquids = false;
    };

    inline Result<RayHit> raycast(const Vec3& origin, const Vec3& direction, RaycastOptions options = {}) {
        abi::Writer w;
        w.put(origin.x).put(origin.y).put(origin.z).put(direction.x).put(direction.y).put(direction.z).put(options.max_distance).boolean(options.include_liquids);
        return detail::call_as<RayHit>(Op::Raycast, w);
    }

    inline Result<AimState> aim() { return detail::call_as<AimState>(Op::Aim); }

    struct FindOptions {
        std::string name{};
        int radius = 8;
        int max_results = 256;
        bool exact = false;
    };

    inline Result<std::vector<abi::FoundBlock>> find_blocks(const BlockPos& center, const FindOptions& options) {
        abi::Writer w;
        w.put(center.x).put(center.y).put(center.z).put(options.radius).put(options.max_results).boolean(options.exact).str(options.name);
        auto bytes = detail::call(Op::FindBlocks, w);
        if (!bytes)
            return std::unexpected(bytes.error());
        abi::Reader r(bytes->data(), bytes->size());
        std::vector<abi::FoundBlock> out;
        const uint32_t n = r.get<uint32_t>();
        for (uint32_t i = 0; i < n && r.Ok(); i++)
            out.push_back(abi::FoundBlock::Read(r));
        return out;
    }

    struct EntityQuery {
        double radius = 0;
        std::string type{};
        bool players_only = false;
        bool include_items = true;
    };

    inline Result<std::vector<Entity>> entities(const EntityQuery& query = {}) {
        abi::Writer w;
        w.put(query.radius).str(query.type).boolean(query.players_only).boolean(query.include_items);
        auto bytes = detail::call(Op::Entities, w);
        if (!bytes)
            return std::unexpected(bytes.error());
        abi::Reader r(bytes->data(), bytes->size());
        std::vector<Entity> out;
        const uint32_t n = r.get<uint32_t>();
        for (uint32_t i = 0; i < n && r.Ok(); i++)
            out.push_back(Entity::Read(r));
        return out;
    }

    inline Result<Entity> entity(uint64_t runtimeId) {
        abi::Writer w;
        w.put(runtimeId);
        return detail::call_as<Entity>(Op::EntityById, w);
    }

    inline Result<std::vector<PlayerEntry>> players() {
        auto bytes = detail::call(Op::PlayerList);
        if (!bytes)
            return std::unexpected(bytes.error());
        abi::Reader r(bytes->data(), bytes->size());
        std::vector<PlayerEntry> out;
        const uint32_t n = r.get<uint32_t>();
        for (uint32_t i = 0; i < n && r.Ok(); i++)
            out.push_back(PlayerEntry::Read(r));
        return out;
    }

    inline Result<ScreenState> screen() { return detail::call_as<ScreenState>(Op::Screen); }

    inline Result<abi::Meta> meta() { return detail::call_as<abi::Meta>(Op::Meta); }

    inline Result<std::string> options_json() {
        auto bytes = detail::call(Op::Options);
        if (!bytes)
            return std::unexpected(bytes.error());
        return std::string(bytes->begin(), bytes->end());
    }

    inline Result<std::string> option(std::string_view key) {
        abi::Writer w;
        w.str(key);
        auto bytes = detail::call(Op::OptionGet, w);
        if (!bytes)
            return std::unexpected(bytes.error());
        return std::string(bytes->begin(), bytes->end());
    }

    inline std::string translate(std::string_view key) {
        abi::Writer w;
        w.str(key);
        auto bytes = detail::call(Op::Translate, w);
        return bytes ? std::string(bytes->begin(), bytes->end()) : std::string(key);
    }

    inline Result<std::vector<std::string>> server_commands() {
        auto bytes = detail::call(Op::ServerCommands);
        if (!bytes)
            return std::unexpected(bytes.error());
        abi::Reader r(bytes->data(), bytes->size());
        std::vector<std::string> out;
        const uint32_t n = r.get<uint32_t>();
        for (uint32_t i = 0; i < n && r.Ok(); i++)
            out.push_back(r.str());
        return out;
    }

    inline Result<std::vector<std::string>> input_actions() {
        auto bytes = detail::call(Op::InputActions);
        if (!bytes)
            return std::unexpected(bytes.error());
        abi::Reader r(bytes->data(), bytes->size());
        std::vector<std::string> out;
        const uint32_t n = r.get<uint32_t>();
        for (uint32_t i = 0; i < n && r.Ok(); i++)
            out.push_back(r.str());
        return out;
    }

    inline bool input_down(std::string_view action) {
        abi::Writer w;
        w.str(action);
        auto bytes = detail::call(Op::InputDown, w);
        return bytes && !bytes->empty() && (*bytes)[0] != 0;
    }

    struct MousePos {
        float x = 0, y = 0;
        bool captured = false;
    };

    inline Result<MousePos> mouse() {
        auto bytes = detail::call(Op::MousePosition);
        if (!bytes)
            return std::unexpected(bytes.error());
        abi::Reader r(bytes->data(), bytes->size());
        MousePos m;
        m.x = r.get<float>();
        m.y = r.get<float>();
        m.captured = r.boolean();
        return m;
    }

    inline int64_t now_ms() {
        auto bytes = detail::call(Op::Clock);
        if (!bytes)
            return 0;
        abi::Reader r(bytes->data(), bytes->size());
        return r.get<int64_t>();
    }
}
