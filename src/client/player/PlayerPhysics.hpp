#pragma once
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
        Box Expand(double dx, double dy, double dz) const;
        Box Inflate(double d) const { return {x0 - d, y0 - d, z0 - d, x1 + d, y1 + d, z1 + d}; }
        bool Intersects(const Box& o) const;
    };

    constexpr int kMaxShapeBoxes = 8;

    struct BlockShape {
        int count = 0;
        Box boxes[kMaxShapeBoxes];
        float friction = 0.6f;
        uint8_t surface = 0;
        uint8_t fluid = 0;
        float fluidLevel = 0.0f;
        uint8_t fluidDepth = 255;
        bool climbable = false;
        bool web = false;
        float speedFactor = 1.0f;
        bool Empty() const { return count == 0; }
    };

    class World {
    public:
        virtual ~World() = default;
        virtual BlockShape Query(int x, int y, int z) const = 0;
    };

    struct Attributes {
        float speed = 0.1f;
        float flySpeed = 0.05f;
        float verticalFlySpeed = 1.0f;
        float jumpHeight = 0.42f;
        int jumpBoost = 0;
        int levitation = 0;
        bool slowFalling = false;
    };

    struct Input {
        float forward = 0;
        float strafe = 0;
        bool jump = false;
        bool sneak = false;
        bool sprint = false;
        bool flying = false;
        bool flyUp = false;
        bool flyDown = false;
        bool noClip = false;
        bool creativeFlight = false;
        bool swimming = false;
        bool headInWater = false;
        float lookY = 0;
        bool gliding = false;
        float javaPitch = 0;
    };

    struct State {
        double x = 0, y = 0, z = 0;
        double vx = 0, vy = 0, vz = 0;
        float yaw = 0;
        bool onGround = false;
        bool horizontalCollision = false;
        bool verticalCollision = false;
        bool inWater = false;
        bool inLava = false;
        bool onClimbable = false;
        int jumpDelay = 0;
        float stuckX = 0, stuckY = 0, stuckZ = 0;

        Box box;
        bool hasBox = false;

        double Height(bool sneaking) const { return sneaking ? kSneakHeight : kStandHeight; }
    };

    Box BoundingBox(const State& state, bool sneaking);

    struct MoveVector {
        float forward = 0, strafe = 0;
    };

    MoveVector ProcessMoveInput(float forward, float strafe, bool sneaking, bool inWater);

    void Step(State& state, const Input& input, const Attributes& attributes, const World& world);
}
