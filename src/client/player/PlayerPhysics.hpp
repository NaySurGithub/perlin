#pragma once
// Client-side player movement prediction for server-authoritative Bedrock servers.
//
// Ported from the vanilla movement code as documented by prismarine-physics and the Java/Bedrock
// player simulation: 20 Hz ticks, gravity 0.08 with 0.98 drag, 0.6 block slipperiness, 0.42 jump
// velocity, 0.5625 step height, Java-style Y -> X/Z collision order with step-up, sneak edge
// protection, ladders, cobwebs, water and lava. It has no engine dependencies so it can be unit tested.
#include <cstdint>

namespace perlin::physics {
    constexpr double kTickSeconds = 0.05;
    constexpr double kWidth = 0.6;
    constexpr double kStandHeight = 1.8;
    constexpr double kSneakHeight = 1.5;
    constexpr double kStandEye = 1.62;
    constexpr double kSneakEye = 1.27;
    constexpr double kStepHeight = 0.5625;
    constexpr double kGravity = 0.08;
    constexpr double kAirDrag = 0.98;
    constexpr double kJumpVelocity = 0.42;
    constexpr double kSprintJumpBoost = 0.2;
    constexpr double kDefaultSlipperiness = 0.6;

    struct Box {
        double x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;

        Box Offset(double dx, double dy, double dz) const { return {x0 + dx, y0 + dy, z0 + dz, x1 + dx, y1 + dy, z1 + dz}; }
        // Grows the box in the direction of a movement (Java AABB.expandTowards).
        Box Expand(double dx, double dy, double dz) const;
        Box Inflate(double d) const { return {x0 - d, y0 - d, z0 - d, x1 + d, y1 + d, z1 + d}; }
        bool Intersects(const Box& o) const;
        // Distance `moving` may travel along one axis before hitting this box.
        double ClipX(const Box& moving, double dx) const;
        double ClipY(const Box& moving, double dy) const;
        double ClipZ(const Box& moving, double dz) const;
    };

    constexpr int kMaxShapeBoxes = 8;

    // Collision/physics description of one block cell. Boxes are in block-local coordinates; they may
    // extend above 1.0 (fences are 1.5 tall).
    struct BlockShape {
        int count = 0;
        Box boxes[kMaxShapeBoxes];
        float friction = 0.6f;
        uint8_t fluid = 0;         // 0 none, 1 water, 2 lava
        float fluidLevel = 0.0f;   // surface height inside the cell, 0..1
        bool climbable = false;
        bool web = false;
        float speedFactor = 1.0f;  // soul sand, honey
        bool Empty() const { return count == 0; }
    };

    class World {
    public:
        virtual ~World() = default;
        virtual BlockShape Query(int x, int y, int z) const = 0;
    };

    struct Attributes {
        float speed = 0.1f;            // minecraft:movement
        float flySpeed = 0.05f;        // abilities
        float verticalFlySpeed = 1.0f; // abilities
    };

    struct Input {
        float forward = 0;   // +1 forward
        float strafe = 0;    // +1 right
        bool jump = false;
        bool sneak = false;
        bool sprint = false; // effective sprinting (the caller resolves toggles, hunger, collisions)
        bool flying = false;
        bool flyUp = false;
        bool flyDown = false;
        bool noClip = false; // spectator: no collision, no gravity, always flying
    };

    struct State {
        double x = 0, y = 0, z = 0; // feet centre
        double vx = 0, vy = 0, vz = 0; // blocks per tick
        float yaw = 0;                 // Bedrock yaw in degrees: 0 faces +Z, 90 faces -X
        bool onGround = false;
        bool horizontalCollision = false;
        bool verticalCollision = false;
        bool inWater = false;
        bool inLava = false;
        bool onClimbable = false;
        int jumpDelay = 0;

        double Height(bool sneaking) const { return sneaking ? kSneakHeight : kStandHeight; }
    };

    Box BoundingBox(const State& state, bool sneaking);

    // Advances one 20 Hz tick.
    void Step(State& state, const Input& input, const Attributes& attributes, const World& world);
}
