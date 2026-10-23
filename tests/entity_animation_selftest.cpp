#include "client/entity/JavaAnimation.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <array>
using namespace perlin;
using namespace perlin::entity::java;
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; ++failures; } } while (0)
bool Near(float a, float b, float epsilon = 0.0001f) { return std::abs(a-b) < epsilon; }
struct Trace { float body, speed, phase; int ticks; };
Trace Run(int fps) {
    BodyRotation body; body.Reset(0);
    WalkAnimation walk;
    double remainder = 0, previousTick = 0;
    int ticks = 0;
    for (int frame = 1; frame <= fps * 6; ++frame) {
        const double dt = 1.0 / fps, end = double(frame) / fps;
        remainder += dt;
        while (remainder + 1e-10 >= 0.05) {
            remainder = std::max(0.0, remainder - 0.05);
            const double t = end - remainder;
            // Strafe, stop, then walk backward while looking across the angle seam.
            const auto position = [](double time) { return std::min(time, 2.0) * 2.0 - std::max(time - 4.0, 0.0) * 2.0; };
            const double dx = position(t) - position(previousTick);
            body.PlayerTick(dx, 0, 179.0f + float(t) * 2, t > 3 && t < 3.5);
            walk.Tick(float(std::abs(dx)), false);
            previousTick = t; ++ticks;
        }
    }
    const float p = float(remainder / 0.05);
    return {body.Sample(p), walk.Speed(p), walk.Position(p), ticks};
}
int main() {
    CHECK(Near(Turn(179, -179, 0.5f), 180));
    BodyRotation b; b.Reset(0);
    b.PlayerTick(0, 0, 40, false); CHECK(Near(b.current, 0));
    b.PlayerTick(0, 0, 100, false); CHECK(Near(b.current, 50));
    b.Reset(0); b.PlayerTick(0.1, 0, 0, false); CHECK(Near(b.current, -27));
    b.Reset(0); b.PlayerTick(0, -0.1, 0, false); CHECK(Near(b.current, 0));
    b.Reset(179); b.PlayerTick(0, 0, -179, true); CHECK(Near(b.current, 179.6f));
    b.Reset(0); for (int i = 0; i < 10; ++i) b.MobTick(0, 0, 0, 40); CHECK(Near(b.current, 0));
    for (int i = 0; i < 11; ++i) b.MobTick(0, 0, 0, 40); CHECK(Near(b.current, 40));
    b.Reset(0); b.MobTick(.1, 0, 90, 100); CHECK(Near(b.current, 90));
    WalkAnimation w; w.Tick(.1f, false); CHECK(Near(w.speed, .16f)); CHECK(Near(w.Position(.5f), .08f));
    w.Tick(.1f, true); CHECK(Near(w.Position(1), 0)); CHECK(Near(w.Speed(1), 0));
    const auto reference = Run(20);
    for (int fps : {10, 15, 30, 60, 144, 240}) {
        auto t = Run(fps);
        CHECK(t.ticks == 120); CHECK(Near(t.body, reference.body, .001f));
        CHECK(Near(t.speed, reference.speed, .001f)); CHECK(Near(t.phase, reference.phase, .001f));
    }
    PoseBlend blend; blend.Tick(true); CHECK(Near(blend.Sample(.5f), .045f));
    for (int i = 0; i < 20; ++i) blend.Tick(true);
    CHECK(Near(blend.current, 1));
    for (int i = 0; i < 20; ++i) blend.Tick(false);
    CHECK(Near(blend.current, 0));
    CHECK(Near(AdvanceAttack(0, .1f, .3f, true), .0001f));
    CHECK(Near(AdvanceAttack(.1f, .03f, .3f, false), .2f));
    CHECK(Near(AdvanceAttack(.9f, .1f, .3f, false), 0));
    PlayerState state; state.age = 0;
    auto p = AnimatePlayer(state);
    CHECK(Near(p.rightArm.rotation.z, .1f)); CHECK(Near(p.leftArm.rotation.z, -.1f));
    state.speed = 1; p = AnimatePlayer(state);
    CHECK(Near(p.rightLeg.rotation.x, 1.4f)); CHECK(Near(p.leftLeg.rotation.x, -1.4f));
    state.riding = true; p = AnimatePlayer(state); CHECK(Near(p.rightLeg.rotation.x, -1.4137167f));
    state.riding = false; state.crouching = true; state.attack = .3f;
    p = AnimatePlayer(state); CHECK(Near(p.head.offset.y, -4.2f)); CHECK(Near(p.body.rotation.x, .5f));
    CHECK(std::abs(p.body.rotation.y) > .01f);
    // The head and arms are children of the torso in Bedrock, siblings in Java.
    // Verify actual world matrices, including attached sleeves, across compound poses.
    attachable::GeometryLibrary lib;
    lib.Load(R"({"minecraft:geometry":[{"description":{"identifier":"geometry.test"},"bones":[
      {"name":"root"},{"name":"waist","parent":"root"},
      {"name":"body","parent":"waist","pivot":[0,24,0]},
      {"name":"head","parent":"body","pivot":[0,24,0]},
      {"name":"rightarm","parent":"body","pivot":[-5,22,0]},
      {"name":"rightsleeve","parent":"rightarm","pivot":[-5,22,0]},
      {"name":"leftarm","parent":"body","pivot":[5,22,0]},
      {"name":"rightleg","parent":"root","pivot":[-1.9,12,0]},
      {"name":"leftleg","parent":"root","pivot":[1.9,12,0]}]}]})");
    const auto* baseGeometry = lib.Find("geometry.test"); CHECK(baseGeometry);
    for (bool armor : {false, true}) {
    attachable::Geometry geometry = *baseGeometry;
    if (armor) {
        int torso = 0;
        for (size_t i = 0; i < geometry.bones.size(); ++i) if (geometry.bones[i].name == "body") torso = int(i);
        for (auto& bone : geometry.bones)
            if (bone.name == "rightleg" || bone.name == "leftleg") bone.parent = torso;
    } else {
        for (auto& bone : geometry.bones)
            if (bone.name == "rightarm" || bone.name == "leftarm") bone.pivot.y -= .5f;
    }
    const auto* g = &geometry;
    for (float swim : {0.0f, .3f, 1.0f}) for (float phase : {0.0f, 13.999f, 14.001f, 21.999f, 22.001f, 25.999f}) {
        state.swim = swim; state.walk = phase; state.headYaw = 50; state.pitch = 35;
        p = AnimatePlayer(state);
        attachable::Pose pose;
        pose["root"].rotation = {20, 30, 40}; pose["rightsleeve"].rotation = {60, 0, 0};
        CHECK(ApplyPlayerPose(p, g, pose));
        std::array<std::pair<const char*, const Part*>,6> parts = {{{"head", &p.head},{"body", &p.body},{"rightarm", &p.rightArm},{"leftarm", &p.leftArm},{"rightleg", &p.rightLeg},{"leftleg", &p.leftLeg}}};
        for (auto [name, part] : parts) {
            glm::mat4 actual; glm::vec3 pivot;
            CHECK(attachable::BoneWorld(*g, pose, name, actual, pivot));
            const glm::mat4 expected = glm::translate(glm::mat4(1), part->offset + pivot) * attachable::EulerZYX(glm::degrees(part->rotation) * glm::vec3(1,-1,1)) * glm::translate(glm::mat4(1), -pivot);
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) CHECK(Near(actual[i][j], expected[i][j], .003f));
        }
        glm::mat4 arm, sleeve; glm::vec3 pivot;
        attachable::BoneWorld(*g, pose, "rightarm", arm, pivot); attachable::BoneWorld(*g, pose, "rightsleeve", sleeve, pivot);
        for (int i=0;i<4;++i) for(int j=0;j<4;++j) CHECK(Near(arm[i][j], sleeve[i][j]));
    }
    }
    // The Euler conversion remains finite at a 90-degree relative head turn.
    p = {}; p.head.rotation = {0.3f, 1.5707963268f, -0.2f};
    attachable::Pose singularPose;
    CHECK(ApplyPlayerPose(p, baseGeometry, singularPose));
    glm::mat4 actual; glm::vec3 pivot;
    attachable::BoneWorld(*baseGeometry, singularPose, "head", actual, pivot);
    auto expected = glm::translate(glm::mat4(1), pivot) * attachable::EulerZYX(glm::degrees(p.head.rotation) * glm::vec3(1,-1,1)) * glm::translate(glm::mat4(1), -pivot);
    for (int i=0;i<4;++i) for(int j=0;j<4;++j) CHECK(Near(actual[i][j], expected[i][j], .003f));
    state = {}; state.usingItem = true; state.right = ArmPose::Bow;
    p = AnimatePlayer(state); CHECK(Near(p.rightArm.rotation.x, -1.5707964f)); CHECK(Near(p.leftArm.rotation.x, -1.5707964f));
    state.right = ArmPose::Spyglass; state.pitch = 90; p = AnimatePlayer(state); CHECK(Near(p.rightArm.rotation.z, 0));
    attachable::Pose empty; CHECK(!ApplyPlayerPose(p, nullptr, empty));
    std::cout << (failures ? "FAIL " : "PASS ") << "entity animation (20 Hz timing, body/head rotation, player poses and bone hierarchy)\n";
    return failures ? 1 : 0;
}
