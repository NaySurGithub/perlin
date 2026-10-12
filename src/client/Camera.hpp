#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace perlin {
    enum class PerspectiveMode { FirstPerson, ThirdPersonBack, ThirdPersonFront };

    struct Camera {
        float x = 0, y = 70, z = 5;
        float yaw = -90.0f, pitch = 0.0f;
        float roll = 0.0f; // degrees around the view direction (hurt shake)
        float fov = 70.0f;
        float aspect = 16.0f / 9.0f;
        float nearPlane = 0.1f;
        float farPlane = 600.0f;
        PerspectiveMode mode = PerspectiveMode::FirstPerson;

        void CycleMode() { mode = static_cast<PerspectiveMode>((static_cast<int>(mode) + 1) % 3); }

        void Rotate(float yawDelta, float pitchDelta) {
            yaw += yawDelta;
            pitch = glm::clamp(pitch + pitchDelta, -89.0f, 89.0f);
        }

        glm::vec3 Direction() const {
            const float yr = glm::radians(yaw);
            const float pr = glm::radians(pitch);
            return {std::cos(yr) * std::cos(pr), std::sin(pr), std::sin(yr) * std::cos(pr)};
        }

        glm::vec3 EyePosition() const {
            const glm::vec3 dir = Direction();
            glm::vec3 eye(x, y, z);
            if (mode == PerspectiveMode::ThirdPersonBack)
                eye -= dir * 5.0f;
            else if (mode == PerspectiveMode::ThirdPersonFront)
                eye += dir * 5.0f;
            return eye;
        }

        glm::vec3 Up() const {
            if (roll == 0.0f)
                return {0.0f, 1.0f, 0.0f};
            return glm::vec3(glm::rotate(glm::mat4(1.0f), glm::radians(roll), Direction()) * glm::vec4(0, 1, 0, 0));
        }

        glm::mat4 SkyViewProjection() const {
            const glm::vec3 dir = Direction();
            const glm::mat4 view = glm::mat4(glm::mat3(glm::lookAt(glm::vec3(0.0f), dir, Up())));
            return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane) * view;
        }

        glm::mat4 ViewProjection() const {
            const glm::vec3 dir = Direction();
            const glm::vec3 eye = EyePosition();

            const glm::mat4 view = glm::lookAt(eye, eye + dir, Up());
            const glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
            return proj * view;
        }
    };
}
