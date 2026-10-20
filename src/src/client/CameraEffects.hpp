#pragma once
#include <glm/glm.hpp>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace perlin {
    struct CameraPose {
        glm::vec3 position{0.0f};
        float yaw = 0.0f;
        float pitch = 0.0f;
    };

    struct CameraSet {
        std::optional<glm::vec3> position;
        std::optional<glm::vec2> rotation;
        std::optional<glm::vec3> facing;
        std::optional<glm::vec2> offset;
        std::optional<glm::vec3> entityOffset;
        int easeType = 0;
        float easeDuration = 0.0f;
        bool hasEase = false;
    };

    class CameraEffects {
    public:
        void Shake(float intensity, float duration, int type);
        void StopShake();
        void Fade(float fadeIn, float wait, float fadeOut, glm::vec3 rgb);
        void SetFov(float fov, float easeTime, int easeType, bool clear);
        void Set(const CameraSet& set);
        void Clear();
        void Target(int64_t entityUid, std::optional<glm::vec3> offset);
        void RemoveTarget();
        void Attach(int64_t entityUid);
        void Detach();
        void SetFog(const std::vector<std::string>& stack);
        std::vector<std::string> FogStack() const;
        void Reset();

        void Tick(const CameraPose& base);

        bool PoseActive() const;
        CameraPose Pose() const;
        glm::mat4 ShakeMatrix() const;
        float Fov(float base) const;
        glm::vec4 Tint() const;
        int64_t TargetUid() const;
        glm::vec3 TargetOffset() const;
        int64_t AttachedUid() const;
        bool Shaking() const;
        void ResolveTarget(const glm::vec3& worldPos, const CameraPose& base);

    private:
        using Clock = std::chrono::steady_clock;
        static double Now();
        static float EaseValue(int type, float t);

        mutable std::mutex mutex;
        float shakeIntensity = 0, shakeDuration = 0;
        double shakeStart = 0;
        int shakeType = 0;
        bool shakeActive = false;
        double shakeNow = 0;

        bool fadeActive = false;
        float fadeIn = 0, fadeWait = 0, fadeOut = 0;
        glm::vec3 fadeColor{0.0f};
        double fadeStart = 0;
        float fadeAlpha = 0;

        bool fovActive = false;
        float fovFrom = 0, fovTo = 0, fovEase = 0;
        int fovType = 0;
        double fovStart = 0;
        bool fovClearing = false;
        float fovCurrent = 0;
        bool fovHave = false;

        bool poseActive = false;
        CameraPose poseFrom, poseTo, poseNow;
        double poseStart = 0;
        float poseDuration = 0;
        int poseType = 0;
        CameraSet lastSet;
        std::optional<glm::vec3> facingPoint;
        int64_t targetUid = 0;
        glm::vec3 targetOffset{0.0f};
        bool targetActive = false;
        int64_t attachedUid = 0;
        std::vector<std::string> fogStack;
        glm::mat4 shakeMatrix{1.0f};
    };
}
