#include "client/CameraEffects.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace perlin {
    namespace {
        constexpr float kPi = 3.14159265358979f;

        float Bounce(float t) {
            constexpr float n = 7.5625f, d = 2.75f;
            if (t < 1.0f / d)
                return n * t * t;
            if (t < 2.0f / d) {
                t -= 1.5f / d;
                return n * t * t + 0.75f;
            }
            if (t < 2.5f / d) {
                t -= 2.25f / d;
                return n * t * t + 0.9375f;
            }
            t -= 2.625f / d;
            return n * t * t + 0.984375f;
        }

        float Hash(float x) {
            const float s = std::sin(x * 12.9898f) * 43758.5453f;
            return s - std::floor(s);
        }

        float Noise(float x) {
            const float i = std::floor(x), f = x - i;
            const float u = f * f * (3.0f - 2.0f * f);
            return (Hash(i) * (1.0f - u) + Hash(i + 1.0f) * u) * 2.0f - 1.0f;
        }
    }

    double CameraEffects::Now() {
        return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
    }

    float CameraEffects::EaseValue(int type, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        const float c1 = 1.70158f, c2 = c1 * 1.525f, c3 = c1 + 1.0f, c4 = 2.0f * kPi / 3.0f,
                    c5 = 2.0f * kPi / 4.5f;
        switch (type) {
        case 0:
        case 32:
            return t;
        case 1:
            return 1.0f - std::cos(t * kPi * 4.5f) * std::exp(-t * 6.0f);
        case 2:
            return t * t;
        case 3:
            return 1.0f - (1.0f - t) * (1.0f - t);
        case 4:
            return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
        case 5:
            return t * t * t;
        case 6:
            return 1.0f - std::pow(1.0f - t, 3.0f);
        case 7:
            return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
        case 8:
            return t * t * t * t;
        case 9:
            return 1.0f - std::pow(1.0f - t, 4.0f);
        case 10:
            return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 4.0f) / 2.0f;
        case 11:
            return t * t * t * t * t;
        case 12:
            return 1.0f - std::pow(1.0f - t, 5.0f);
        case 13:
            return t < 0.5f ? 16.0f * std::pow(t, 5.0f) : 1.0f - std::pow(-2.0f * t + 2.0f, 5.0f) / 2.0f;
        case 14:
            return 1.0f - std::cos(t * kPi / 2.0f);
        case 15:
            return std::sin(t * kPi / 2.0f);
        case 16:
            return -(std::cos(kPi * t) - 1.0f) / 2.0f;
        case 17:
            return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
        case 18:
            return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
        case 19:
            return t == 0.0f   ? 0.0f
                   : t == 1.0f ? 1.0f
                   : t < 0.5f  ? std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f
                               : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;
        case 20:
            return 1.0f - std::sqrt(1.0f - t * t);
        case 21:
            return std::sqrt(1.0f - (t - 1.0f) * (t - 1.0f));
        case 22:
            return t < 0.5f ? (1.0f - std::sqrt(1.0f - std::pow(2.0f * t, 2.0f))) / 2.0f
                            : (std::sqrt(1.0f - std::pow(-2.0f * t + 2.0f, 2.0f)) + 1.0f) / 2.0f;
        case 23:
            return 1.0f - Bounce(1.0f - t);
        case 24:
            return Bounce(t);
        case 25:
            return t < 0.5f ? (1.0f - Bounce(1.0f - 2.0f * t)) / 2.0f
                            : (1.0f + Bounce(2.0f * t - 1.0f)) / 2.0f;
        case 26:
            return c3 * t * t * t - c1 * t * t;
        case 27:
            return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
        case 28:
            return t < 0.5f
                       ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) / 2.0f
                       : (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) /
                             2.0f;
        case 29:
            return t == 0.0f   ? 0.0f
                   : t == 1.0f ? 1.0f
                               : -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
        case 30:
            return t == 0.0f   ? 0.0f
                   : t == 1.0f ? 1.0f
                               : std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
        case 31:
            return t == 0.0f   ? 0.0f
                   : t == 1.0f ? 1.0f
                   : t < 0.5f
                       ? -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f
                       : (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f +
                             1.0f;
        default:
            return t;
        }
    }

    void CameraEffects::Shake(float intensity, float duration, int type) {
        std::lock_guard lock(mutex);
        shakeIntensity = std::clamp(intensity, 0.0f, 4.0f);
        shakeDuration = std::max(duration, 0.0f);
        shakeType = type;
        shakeStart = Now();
        shakeActive = shakeDuration > 0.0f && shakeIntensity > 0.0f;
    }

    void CameraEffects::StopShake() {
        std::lock_guard lock(mutex);
        shakeActive = false;
        shakeMatrix = glm::mat4(1.0f);
    }

    void CameraEffects::Fade(float in, float wait, float out, glm::vec3 rgb) {
        std::lock_guard lock(mutex);
        fadeIn = std::max(in, 0.0f);
        fadeWait = std::max(wait, 0.0f);
        fadeOut = std::max(out, 0.0f);
        fadeColor = rgb;
        fadeStart = Now();
        fadeActive = true;
    }

    void CameraEffects::SetFov(float fov, float easeTime, int easeType, bool clear) {
        std::lock_guard lock(mutex);
        fovFrom = fovHave ? fovCurrent : 0.0f;
        fovStart = Now();
        fovEase = std::max(easeTime, 0.0f);
        fovType = easeType;
        fovClearing = clear;
        fovTo = fov;
        fovActive = true;
        if (!fovHave)
            fovFrom = 0.0f;
    }

    void CameraEffects::Set(const CameraSet& set) {
        std::lock_guard lock(mutex);
        lastSet = set;
        poseFrom = poseActive ? poseNow : CameraPose{};
        const bool wasActive = poseActive;
        poseActive = true;
        poseStart = Now();
        poseDuration = set.hasEase ? set.easeDuration : 0.0f;
        poseType = set.easeType;
        if (!wasActive)
            poseFrom = CameraPose{};
        facingPoint = set.facing;
        if (set.position)
            poseTo.position = *set.position;
        if (set.rotation) {
            poseTo.pitch = -set.rotation->x;
            poseTo.yaw = set.rotation->y + 90.0f;
        }
        if (!wasActive)
            poseFrom = poseTo;
    }

    void CameraEffects::Clear() {
        std::lock_guard lock(mutex);
        poseActive = false;
        facingPoint.reset();
        fovActive = false;
        fovHave = false;
        fadeActive = false;
        shakeActive = false;
        shakeMatrix = glm::mat4(1.0f);
        targetActive = false;
        targetUid = 0;
        attachedUid = 0;
    }

    void CameraEffects::Target(int64_t entityUid, std::optional<glm::vec3> offset) {
        std::lock_guard lock(mutex);
        targetUid = entityUid;
        targetOffset = offset ? *offset : glm::vec3(0.0f);
        targetActive = true;
    }

    void CameraEffects::RemoveTarget() {
        std::lock_guard lock(mutex);
        targetActive = false;
        targetUid = 0;
    }

    void CameraEffects::Attach(int64_t entityUid) {
        std::lock_guard lock(mutex);
        attachedUid = entityUid;
    }

    void CameraEffects::Detach() {
        std::lock_guard lock(mutex);
        attachedUid = 0;
    }

    void CameraEffects::SetFog(const std::vector<std::string>& stack) {
        std::lock_guard lock(mutex);
        fogStack = stack;
    }

    std::vector<std::string> CameraEffects::FogStack() const {
        std::lock_guard lock(mutex);
        return fogStack;
    }

    void CameraEffects::Reset() {
        Clear();
        std::lock_guard lock(mutex);
        fogStack.clear();
        poseTo = CameraPose{};
    }

    void CameraEffects::Tick(const CameraPose& base) {
        std::lock_guard lock(mutex);
        const double now = Now();
        shakeMatrix = glm::mat4(1.0f);
        if (shakeActive) {
            const float elapsed = static_cast<float>(now - shakeStart);
            if (elapsed >= shakeDuration)
                shakeActive = false;
            else {
                const float fade =
                    std::min(1.0f, (shakeDuration - elapsed) / std::max(0.25f, shakeDuration * 0.35f));
                const float amp = shakeIntensity * fade;
                const float t = elapsed * 22.0f;
                const float nx = Noise(t), ny = Noise(t + 31.7f), nz = Noise(t + 77.3f);
                if (shakeType == 1) {
                    const float deg = amp * 2.2f;
                    shakeMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(nz * deg), glm::vec3(0, 0, 1)) *
                                  glm::rotate(glm::mat4(1.0f), glm::radians(ny * deg), glm::vec3(1, 0, 0)) *
                                  glm::rotate(glm::mat4(1.0f), glm::radians(nx * deg), glm::vec3(0, 1, 0));
                } else {
                    const float d = amp * 0.12f;
                    shakeMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(nx * d, ny * d, nz * d * 0.5f));
                }
            }
        }
        if (fadeActive) {
            const float elapsed = static_cast<float>(now - fadeStart);
            if (elapsed < fadeIn)
                fadeAlpha = fadeIn > 0.0f ? elapsed / fadeIn : 1.0f;
            else if (elapsed < fadeIn + fadeWait)
                fadeAlpha = 1.0f;
            else if (elapsed < fadeIn + fadeWait + fadeOut)
                fadeAlpha = 1.0f - (elapsed - fadeIn - fadeWait) / std::max(fadeOut, 1e-4f);
            else {
                fadeActive = false;
                fadeAlpha = 0.0f;
            }
        } else
            fadeAlpha = 0.0f;
        if (fovActive) {
            const float t = fovEase > 0.0f ? static_cast<float>((now - fovStart) / fovEase) : 1.0f;
            const float e = EaseValue(fovType, t);
            if (fovClearing) {
                fovCurrent = 0.0f;
                if (t >= 1.0f) {
                    fovActive = false;
                    fovHave = false;
                }
            } else {
                const float from = fovHave && fovFrom > 0.0f ? fovFrom : fovTo;
                fovCurrent = from + (fovTo - from) * e;
                fovHave = true;
                if (t >= 1.0f)
                    fovActive = false;
            }
        }
        if (poseActive) {
            CameraPose target = poseTo;
            if (!lastSet.position)
                target.position = base.position;
            if (!lastSet.rotation && !facingPoint)
                target.yaw = base.yaw, target.pitch = base.pitch;
            if (facingPoint) {
                const glm::vec3 d = *facingPoint - target.position;
                const float horizontal = std::sqrt(d.x * d.x + d.z * d.z);
                target.yaw = glm::degrees(std::atan2(d.z, d.x));
                target.pitch = glm::degrees(std::atan2(d.y, horizontal));
            }
            if (lastSet.offset)
                target.position += glm::vec3(lastSet.offset->x, lastSet.offset->y, 0.0f);
            const float t = poseDuration > 0.0f ? static_cast<float>((now - poseStart) / poseDuration) : 1.0f;
            const float e = EaseValue(poseType, t);
            CameraPose from = poseFrom;
            if (t >= 1.0f)
                poseNow = target;
            else {
                float dyaw = target.yaw - from.yaw;
                while (dyaw > 180.0f)
                    dyaw -= 360.0f;
                while (dyaw < -180.0f)
                    dyaw += 360.0f;
                poseNow.position = from.position + (target.position - from.position) * e;
                poseNow.yaw = from.yaw + dyaw * e;
                poseNow.pitch = from.pitch + (target.pitch - from.pitch) * e;
            }
            if (t >= 1.0f)
                poseFrom = poseNow;
        }
    }

    void CameraEffects::ResolveTarget(const glm::vec3& worldPos, const CameraPose& base) {
        std::lock_guard lock(mutex);
        if (!targetActive)
            return;
        const glm::vec3 from = poseActive ? poseNow.position : base.position;
        const glm::vec3 d = worldPos + targetOffset - from;
        const float horizontal = std::sqrt(d.x * d.x + d.z * d.z);
        poseNow.position = from;
        poseNow.yaw = glm::degrees(std::atan2(d.z, d.x));
        poseNow.pitch = glm::degrees(std::atan2(d.y, horizontal));
        poseActive = true;
    }

    bool CameraEffects::PoseActive() const {
        std::lock_guard lock(mutex);
        return poseActive;
    }

    CameraPose CameraEffects::Pose() const {
        std::lock_guard lock(mutex);
        return poseNow;
    }

    glm::mat4 CameraEffects::ShakeMatrix() const {
        std::lock_guard lock(mutex);
        return shakeMatrix;
    }

    float CameraEffects::Fov(float base) const {
        std::lock_guard lock(mutex);
        return fovHave && fovCurrent > 1.0f ? fovCurrent : base;
    }

    glm::vec4 CameraEffects::Tint() const {
        std::lock_guard lock(mutex);
        return glm::vec4(fadeColor, std::clamp(fadeAlpha, 0.0f, 1.0f));
    }

    int64_t CameraEffects::TargetUid() const {
        std::lock_guard lock(mutex);
        return targetActive ? targetUid : 0;
    }

    glm::vec3 CameraEffects::TargetOffset() const {
        std::lock_guard lock(mutex);
        return targetOffset;
    }

    int64_t CameraEffects::AttachedUid() const {
        std::lock_guard lock(mutex);
        return attachedUid;
    }

    bool CameraEffects::Shaking() const {
        std::lock_guard lock(mutex);
        return shakeActive;
    }
}
