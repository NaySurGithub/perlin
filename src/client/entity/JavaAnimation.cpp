#include "client/entity/JavaAnimation.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <numbers>

namespace perlin::entity::java {
namespace {
constexpr float pi = std::numbers::pi_v<float>;
float Lerp(float a, float b, float t) { return a + (b - a) * t; }
float RotLerp(float a, float b, float t) { return a + Wrap(glm::degrees(b - a)) * (pi / 180.0f) * t; }
void Arm(PlayerPose& p, const PlayerState& s, bool right, ArmPose mode) {
    Part& a = right ? p.rightArm : p.leftArm;
    const float sign = right ? -1.0f : 1.0f;
    switch (mode) {
    case ArmPose::Empty: a.rotation.y = 0; break;
    case ArmPose::Item: a.rotation.x = a.rotation.x * 0.5f - pi / 10; a.rotation.y = 0; break;
    case ArmPose::Block:
        a.rotation.x = a.rotation.x * 0.5f - 0.9424779f + std::clamp(p.head.rotation.x, -1.3962634f, 0.43633232f);
        a.rotation.y = sign * pi / 6 + std::clamp(p.head.rotation.y, -pi / 6, pi / 6); break;
    case ArmPose::Trident: a.rotation.x = a.rotation.x * 0.5f - pi; a.rotation.y = 0; break;
    case ArmPose::Bow:
        p.rightArm.rotation = {-pi / 2 + p.head.rotation.x, p.head.rotation.y - 0.1f - (right ? 0 : 0.4f), 0};
        p.leftArm.rotation = {-pi / 2 + p.head.rotation.x, p.head.rotation.y + 0.1f + (right ? 0.4f : 0), 0}; break;
    case ArmPose::CrossbowCharge: {
        Part& other = right ? p.leftArm : p.rightArm;
        a.rotation = {-0.97079635f, sign * 0.8f, 0};
        const float charge = std::clamp(s.useTicks / 25.0f, 0.0f, 1.0f);
        other.rotation = {Lerp(a.rotation.x, -pi / 2, charge), -sign * Lerp(0.4f, 0.85f, charge), 0}; break;
    }
    case ArmPose::CrossbowHold: {
        Part& other = right ? p.leftArm : p.rightArm;
        a.rotation = {-pi / 2 + p.head.rotation.x + 0.1f, sign * 0.3f + p.head.rotation.y, 0};
        other.rotation = {-1.5f + p.head.rotation.x, -sign * 0.6f + p.head.rotation.y, 0}; break;
    }
    case ArmPose::Spyglass:
        a.rotation = {std::clamp(p.head.rotation.x - 1.9198622f - (s.crouching ? pi / 12 : 0), -2.4f, 3.3f), p.head.rotation.y + sign * pi / 12, 0}; break;
    case ArmPose::Horn:
        a.rotation = {std::clamp(p.head.rotation.x, -1.2f, 1.2f) - 1.4835298f, p.head.rotation.y + sign * pi / 6, 0}; break;
    case ArmPose::Brush: a.rotation.x = a.rotation.x * 0.5f - pi / 5; a.rotation.y = 0; break;
    }
}
bool TwoHanded(ArmPose p) { return p == ArmPose::Bow || p == ArmPose::CrossbowCharge || p == ArmPose::CrossbowHold; }
}
PlayerPose AnimatePlayer(const PlayerState& s) {
    PlayerPose p;
    p.head.rotation = {glm::radians(s.pitch), glm::radians(std::clamp(s.headYaw, -85.0f, 85.0f)), 0};
    if (s.gliding) p.head.rotation.x = -pi / 4;
    const float swing = std::cos(s.walk * 0.6662f) * s.speed / std::max(s.speedScale, 1.0f);
    p.rightArm.rotation.x = -swing;
    p.leftArm.rotation.x = swing;
    p.rightLeg.rotation = {swing * 1.4f, 0.005f, 0.005f};
    p.leftLeg.rotation = {-swing * 1.4f, -0.005f, -0.005f};
    if (s.riding) {
        p.rightArm.rotation.x -= pi / 5; p.leftArm.rotation.x -= pi / 5;
        p.rightLeg.rotation = {-pi * 0.45f, pi / 10, pi / 40};
        p.leftLeg.rotation = {-pi * 0.45f, -pi / 10, -pi / 40};
    }
    // The active hand wins when a two-handed item occupies both arms.
    if (TwoHanded(s.left) && !s.usingItem) { Arm(p, s, true, s.right); Arm(p, s, false, s.left); }
    else { Arm(p, s, false, s.left); Arm(p, s, true, s.right); }
    const float attack = std::clamp(s.attack, 0.0f, 1.0f);
    if (attack > 0) {
        const float twist = std::sin(std::sqrt(attack) * pi * 2) * 0.2f;
        p.body.rotation.y = twist;
        p.rightArm.offset = {5 * (1 - std::cos(twist)), 0, 5 * std::sin(twist)};
        p.leftArm.offset = {-p.rightArm.offset.x, 0, -p.rightArm.offset.z};
        p.rightArm.rotation.y += twist * 3;
        p.leftArm.rotation.y += twist;
        p.leftArm.rotation.x += twist;
        const float eased = 1 - std::pow(1 - attack, 4);
        p.rightArm.rotation.x -= std::sin(eased * pi) * 1.2f + std::sin(attack * pi) * -(p.head.rotation.x - 0.7f) * 0.75f;
        p.rightArm.rotation.z -= std::sin(attack * pi) * 0.4f;
    }
    if (s.crouching) {
        p.body.rotation.x = 0.5f;
        p.rightArm.rotation.x += 0.4f; p.leftArm.rotation.x += 0.4f;
        p.rightLeg.offset.z = p.leftLeg.offset.z = 4;
        p.head.offset.y = -4.2f;
        p.body.offset.y = p.rightArm.offset.y = p.leftArm.offset.y = -3.2f;
    }
    const auto bob = [&](Part& part, float sign) {
        part.rotation.z += sign * (std::cos(s.age * 0.09f) * 0.05f + 0.05f);
        part.rotation.x += sign * std::sin(s.age * 0.067f) * 0.05f;
    };
    if (s.right != ArmPose::Spyglass) bob(p.rightArm, 1);
    if (s.left != ArmPose::Spyglass) bob(p.leftArm, -1);
    const float swim = std::clamp(s.swim, 0.0f, 1.0f);
    if (swim > 0) {
        if (!s.gliding) p.head.rotation.x = RotLerp(p.head.rotation.x, -pi / 4, swim);
        const float phase = std::fmod(s.walk, 26.0f);
        float x = 0, z = 0;
        if (phase < 14) z = 1.8707964f * (-65 * phase + phase * phase) / (-65 * 14 + 14 * 14);
        else if (phase < 22) { const float t = (phase - 14) / 8; x = pi / 2 * t; z = 1.8707964f * (1 - t); }
        else x = pi / 2 * (1 - (phase - 22) / 4);
        if (!s.usingItem) {
            auto& l = p.leftArm.rotation;
            l = {RotLerp(l.x, x, swim), RotLerp(l.y, pi, swim), RotLerp(l.z, pi + z, swim)};
            if (attack <= 0 && s.right != ArmPose::Trident) {
                auto& r = p.rightArm.rotation;
                r = {Lerp(r.x, x, swim), Lerp(r.y, pi, swim), Lerp(r.z, pi - z, swim)};
            }
        }
        p.leftLeg.rotation.x = Lerp(p.leftLeg.rotation.x, 0.3f * std::cos(s.walk / 3 + pi), swim);
        p.rightLeg.rotation.x = Lerp(p.rightLeg.rotation.x, 0.3f * std::cos(s.walk / 3), swim);
    }
    return p;
}

bool ApplyPlayerPose(const PlayerPose& p, const attachable::Geometry* g, attachable::Pose& pose) {
    if (!g) return false;
    const std::array<std::pair<const char*, const Part*>, 6> parts = {{{"head", &p.head}, {"body", &p.body}, {"rightarm", &p.rightArm}, {"leftarm", &p.leftArm}, {"rightleg", &p.rightLeg}, {"leftleg", &p.leftLeg}}};
    for (const auto& [name, part] : parts)
        if (std::none_of(g->bones.begin(), g->bones.end(), [&](const auto& b) { return b.name == name; })) return false;
    // Standard player controllers also animate these ancestors. Clear their old
    // contributions before fitting the Java parts, avoiding double swim/crouch.
    for (const char* name : {"root", "waist", "hat", "jacket", "leftsleeve", "rightsleeve", "leftpants", "rightpants"}) pose.erase(name);
    std::vector<glm::mat4> world(g->bones.size(), glm::mat4(1));
    for (size_t i = 0; i < g->bones.size(); ++i) {
        const auto& b = g->bones[i];
        const auto found = std::find_if(parts.begin(), parts.end(), [&](const auto& part) { return b.name == part.first; });
        const glm::mat4 parent = b.parent >= 0 ? world[b.parent] : glm::mat4(1);
        auto& xf = pose[b.name];
        if (found != parts.end()) {
            const auto& part = *found->second;
            const glm::vec3 degrees = glm::degrees(part.rotation) * glm::vec3(1, -1, 1);
            const glm::mat4 desired = glm::translate(glm::mat4(1), part.offset + b.pivot) * attachable::EulerZYX(degrees) * glm::translate(glm::mat4(1), -b.pivot);
            const glm::mat4 fit = glm::inverse(parent) * desired;
            const float y = std::asin(std::clamp(-fit[0][2], -1.0f, 1.0f));
            const bool singular = std::hypot(fit[0][0], fit[0][1]) < 1e-5f;
            const float x = singular ? std::atan2(-fit[2][1], fit[1][1]) : std::atan2(fit[1][2], fit[2][2]);
            const float z = singular ? 0.0f : std::atan2(fit[0][1], fit[0][0]);
            const glm::vec3 rotation = -glm::degrees(glm::vec3(x, y, z));
            xf.rotation = rotation - b.rotation;
            xf.position = glm::vec3(fit[3]) - b.pivot + glm::mat3(attachable::EulerZYX(rotation)) * b.pivot;
            xf.scale = glm::vec3(1);
        }
        world[i] = parent * glm::translate(glm::mat4(1), xf.position + b.pivot) * attachable::EulerZYX(b.rotation + xf.rotation) * glm::scale(glm::mat4(1), xf.scale) * glm::translate(glm::mat4(1), -b.pivot);
    }
    return true;
}
}
