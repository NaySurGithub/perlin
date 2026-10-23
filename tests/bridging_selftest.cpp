#include "client/player/Bridging.hpp"
#include "client/player/BlockUse.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>

using namespace perlin;
using namespace perlin::bridging;

static int checks = 0, failures = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

constexpr physics::Box kCube{0, 0, 0, 1, 1, 1};
constexpr physics::Box kSlab{0, 0, 0, 1, 0.5, 1};

struct World {
    std::map<std::array<int, 3>, Cell> cells;
    Cell Read(glm::ivec3 p) const {
        const auto found = cells.find({p.x, p.y, p.z});
        return found == cells.end() ? Cell{true, true, false, {}} : found->second;
    }
    void Solid(glm::ivec3 p, physics::Box shape = kCube) { cells[{p.x, p.y, p.z}] = {true, false, false, {shape}}; }
    std::optional<Target> Find(const Request& request) const {
        return FindTarget(request, [&](glm::ivec3 p) { return Read(p); });
    }
};

static Request Looking(glm::dvec3 feet, glm::dvec3 at, bool sneak = false) {
    Request request;
    request.eye = feet + glm::dvec3(0, sneak ? 1.27 : 1.62, 0);
    request.direction = at - request.eye;
    request.body = {feet.x - 0.3, feet.y, feet.z - 0.3, feet.x + 0.3, feet.y + (sneak ? 1.5 : 1.8), feet.z + 0.3};
    request.sneaking = sneak;
    return request;
}

int main() {
    CHECK(blockuse::IsReplaceableCell(true, 0, ""));
    CHECK(blockuse::IsReplaceableCell(true, 1, "minecraft:air"));
    CHECK(blockuse::IsReplaceableCell(true, 2, "minecraft:water"));
    CHECK(blockuse::IsReplaceableCell(true, 3, "minecraft:short_grass"));
    CHECK(!blockuse::IsReplaceableCell(false, 0, "minecraft:air"));
    CHECK(!blockuse::IsReplaceableCell(false, 2, "minecraft:water"));
    CHECK(!blockuse::IsReplaceableCell(true, 4, ""));
    CHECK(!blockuse::IsReplaceableCell(true, 5, "minecraft:stone"));
    CHECK(blockuse::IsUpperSlabPlacement(0, 0));
    CHECK(!blockuse::IsUpperSlabPlacement(1, 1));
    for (int face = 2; face <= 5; ++face) {
        CHECK(!blockuse::IsUpperSlabPlacement(face, 0.499f));
        CHECK(!blockuse::IsUpperSlabPlacement(face, 0.5f));
        CHECK(blockuse::IsUpperSlabPlacement(face, 0.501f));
    }
    // Each cardinal direction must produce an actual neighbouring cell, the
    // matching Bedrock face, and a click on that side of the correct support.
    const std::array<glm::ivec3, 4> offsets{glm::ivec3(0, 0, -1), {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
    for (int direction = 0; direction < 4; ++direction)
        for (int negative = 0; negative < 2; ++negative)
            for (bool crouch : {false, true}) {
                const glm::ivec3 anchor = negative ? glm::ivec3(-17, -2, -17) : glm::ivec3(0, 0, 0);
                const glm::dvec3 feet = glm::dvec3(anchor) + glm::dvec3(0.5, 1, 0.5);
                const glm::dvec3 aim = feet + glm::dvec3(offsets[direction]) * 0.75;
                World world;
                world.Solid(anchor);
                const auto target = world.Find(Looking(feet, aim, crouch));
                CHECK(target.has_value());
                if (!target) continue;
                CHECK(target->anchor == anchor);
                CHECK(target->destination == anchor + offsets[direction]);
                CHECK(target->face == direction + 2);
                const auto click = target->point - glm::dvec3(anchor);
                CHECK(click.x >= 0 && click.x <= 1 && click.y >= 0 && click.y <= 1 && click.z >= 0 && click.z <= 1);
                CHECK((direction == 0 && click.z == 0) || (direction == 1 && click.z == 1) ||
                      (direction == 2 && click.x == 0) || (direction == 3 && click.x == 1));
                int clicked[3]{anchor.x, anchor.y, anchor.z};
                const auto use = blockuse::Classify("minecraft:stone", "minecraft:air", clicked, target->face, true, true, crouch);
                CHECK(use.use == blockuse::Use::Place);
                CHECK(glm::ivec3(use.dest[0], use.dest[1], use.dest[2]) == target->destination);
            }

    World world;
    world.Solid({0, 0, 0});
    Request request = Looking({0.5, 1, 0.5}, {1.25, 1, 0.5});
    const auto baseline = world.Find(request);
    CHECK(baseline && baseline->anchor.y == 0); // Previously the helper incorrectly looked at Y=-1.

    // The player's centre has crossed the edge; the remaining footprint still
    // overlaps the old support. Jumping does not require onGround membership.
    for (double height : {1.0, 1.05, 1.25, 2.25, 2.5}) {
        const auto target = world.Find(Looking({1.1, height, 0.5}, {1.4, 1, 0.5}));
        CHECK(target && target->anchor == glm::ivec3(0) && target->destination == glm::ivec3(1, 0, 0));
    }
    CHECK(!world.Find(Looking({1.31, 1, 0.5}, {1.5, 1, 0.5}))); // No actual support under the footprint.
    CHECK(!world.Find(Looking({0.5, 2.51, 0.5}, {1.25, 1, 0.5})));
    CHECK(!world.Find(Looking({0.5, 0.9, 0.5}, {1.25, 1, 0.5})));

    // A diagonal route needs a side-connected step; corner-only anchors cannot
    // place a floating diagonal block. A neighbouring step supplies that face.
    CHECK(!world.Find(Looking({0.5, 1, 0.5}, {1.2, 1, 1.2})));
    world.Solid({1, 0, 0});
    const auto diagonal = world.Find(Looking({0.9, 1, 0.9}, {1.2, 1, 1.2}));
    CHECK(diagonal && diagonal->anchor == glm::ivec3(1, 0, 0) && diagonal->destination == glm::ivec3(1, 0, 1));

    world = {};
    world.Solid({0, 0, 0}, kSlab);
    const auto slab = world.Find(Looking({0.5, 0.5, 0.5}, {1.25, 0.5, 0.5}));
    CHECK(slab && slab->anchor.y == 0 && std::abs(slab->point.y - 0.499) < 1e-6);
    request = Looking({1.1, 0.5, 0.5}, {1.4, 0.5, 0.5});
    CHECK(!world.Find(request)); // A full block would enclose the player above a lower slab.
    const std::array slabShape{kSlab};
    request.placedShape = slabShape;
    request.horizontalSlab = true;
    CHECK(world.Find(request).has_value()); // A lower slab only touches the player's feet.

    world.Solid({0, 0, 0}, kCube);
    request = Looking({0.5, 1, 0.5}, {1.25, 1, 0.5});
    request.placedShape = slabShape;
    request.horizontalSlab = true;
    const auto upperSlab = world.Find(request);
    CHECK(upperSlab && upperSlab->placedShape.size() == 1 && upperSlab->placedShape[0].y0 == 0.5 && upperSlab->placedShape[0].y1 == 1);
    world.Solid({0, 0, 0}, {0, 0, 0, 1, 0.75, 1});
    request = Looking({1.1, 0.75, 0.5}, {1.4, 0.75, 0.5});
    request.placedShape = slabShape;
    request.horizontalSlab = true;
    CHECK(!world.Find(request)); // The selected upper slab would overlap the player's feet.

    world = {};
    world.Solid({0, 0, 0}, {0.375, 0, 0.375, 0.625, 1.5, 0.625});
    const auto fence = world.Find(Looking({0.5, 1.5, 0.5}, {1.2, 1.5, 0.5}));
    CHECK(fence && fence->point.y <= 1 && fence->point.x == 0.625);
    CHECK(!world.Find(Looking({1.0, 1.5, 0.5}, {1.4, 1.5, 0.5}))); // Narrow shape cannot support this footprint.

    world = {};
    world.Solid({0, 0, 0});
    world.cells[{1, 0, 0}] = {false, true, false, {}};
    request = Looking({0.5, 1, 0.5}, {1.25, 1, 0.5});
    CHECK(!world.Find(request)); // An unloaded destination is not air.
    world.Solid({1, 0, 0});
    CHECK(!world.Find(request));
    world.cells.erase({1, 0, 0});
    world.cells[{0, 0, 0}].loaded = false;
    CHECK(!world.Find(request));
    world.cells[{0, 0, 0}].loaded = true;
    world.cells[{0, 0, 0}].replaceable = true;
    CHECK(!world.Find(request)); // Plants, liquids and other replaceable cells do not anchor a bridge.
    world.cells[{0, 0, 0}].replaceable = false;
    world.cells[{0, 0, 0}].interactive = true;
    CHECK(!world.Find(request)); // Air use must not unexpectedly open an interactive support.
    request.sneaking = true;
    CHECK(world.Find(request).has_value());
    world.cells[{0, 0, 0}].interactive = false;

    request = Looking({0.5, 1, 0.5}, {1.25, 1, 0.5});
    request.reach = 1;
    CHECK(!world.Find(request));
    request.reach = 5.7;
    request.direction = {1, 0, 0};
    CHECK(!world.Find(request));
    request.direction = {0, 1, 0};
    CHECK(!world.Find(request));
    request.direction = {0, 0, 0};
    CHECK(!world.Find(request));
    request.direction = {std::numeric_limits<double>::quiet_NaN(), -1, 0};
    CHECK(!world.Find(request));
    request.direction = {1, -1, 0};
    request.body.x0 = std::numeric_limits<double>::infinity();
    CHECK(!world.Find(request));

    PlacementRepeat repeat;
    CHECK(repeat.Ready(1000, {1, 0, 0}, 42, false));
    repeat.Record(1000, {1, 0, 0}, 42);
    CHECK(!repeat.Ready(1199, {1, 0, 0}, 42, false));
    CHECK(repeat.Ready(1200, {1, 0, 0}, 42, false));
    CHECK(!repeat.Ready(1049, {2, 0, 0}, 42, false));
    CHECK(repeat.Ready(1050, {2, 0, 0}, 42, false));
    CHECK(repeat.Ready(1050, {1, 0, 0}, 43, false));
    CHECK(repeat.Ready(1001, {1, 0, 0}, 42, true));
    CHECK(repeat.Ready(999, {1, 0, 0}, 42, false));
    repeat.Reset();
    CHECK(repeat.Ready(1001, {1, 0, 0}, 42, false));

    // Held-use routes span chunk borders and a moving/jumping footprint. Each
    // predicted block becomes the next physical anchor; the target is recomputed
    // on every frame rather than repeating one saved hit or stacking above it.
    for (int material : {0, 1, 2}) for (bool jumping : {false, true}) {
        World route;
        const physics::Box initial = material == 1 ? kSlab : material == 2 ? physics::Box{0, 0.5, 0, 1, 1, 1} : kCube;
        route.Solid({0, 0, 0}, initial);
        PlacementRepeat held;
        int placed = 0;
        for (int tick = 0; tick < 160; ++tick) {
            const double x = 0.5 + tick * 0.27;
            const double y = initial.y1 + (jumping ? 1.25 * std::sin((tick % 12) * 3.141592653589793 / 12) : 0);
            auto move = Looking({x, y, 0.5}, {x + 0.85, initial.y1, 0.5});
            // Keep yaw/pitch fixed throughout the jump, as a held-use player
            // does; do not help the solver by tracking a point on the floor.
            move.direction = {0.85, -1.62, 0};
            if (material) {
                move.placedShape = slabShape;
                move.horizontalSlab = true;
            }
            CHECK(!route.Read({static_cast<int>(std::floor(x)), 0, 0}).replaceable);
            const auto target = route.Find(move);
            if (!target || !held.Ready(tick * 50, target->destination, 42, tick == 0)) continue;
            CHECK(target->destination == target->anchor + glm::ivec3(1, 0, 0));
            CHECK(target->destination.y == 0);
            CHECK(!OverlapsPlacement(move.body, target->destination, target->placedShape));
            CHECK(target->placedShape.size() == 1 && target->placedShape[0].y1 == initial.y1);
            route.Solid(target->destination, target->placedShape[0]);
            held.Record(tick * 50, target->destination, 42);
            ++placed;
        }
        CHECK(placed >= 40);
        CHECK(!route.Read({43, 0, 0}).replaceable);
    }

    std::printf("Bridging: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
