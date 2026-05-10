#include <algorithm>
#include <cmath>
#include <rtengine/camera_controller.hpp>

namespace rt {
CameraController::CameraController(const CameraControllerSettings& s) : settings(s) {}

void CameraController::update(Camera& camera, const CameraInput& input, float frameSeconds) const {
    const auto& s = settings;
    const float dt = std::min(frameSeconds, s.maxFrameSeconds);
    auto rotation = camera.transform.rotation;

    if (input.mouseDelta != glm::vec2(0)) {
        // use current pose so external edits work
        const auto forward = rotation * glm::vec3(0, 0, -1);
        float yaw = std::atan2(-forward.x, -forward.z);
        float pitch = std::asin(std::clamp(forward.y, -1.f, 1.f));
        yaw -= input.mouseDelta.x * s.mouseSensitivity;
        pitch += input.mouseDelta.y * s.mouseSensitivity * (s.invertY ? 1.f : -1.f);
        pitch = std::clamp(pitch, -s.maxPitch, s.maxPitch);
        rotation =
            glm::angleAxis(yaw, glm::vec3(0, 1, 0)) * glm::angleAxis(pitch, glm::vec3(1, 0, 0));
        camera.transform.rotation = rotation;
    }

    if (input.movement == glm::vec3(0))
        return;

    const float amount = std::min(glm::length(input.movement), 1.f);
    const auto up = s.worldUpMovement ? glm::vec3(0, 1, 0) : rotation * glm::vec3(0, 1, 0);
    const auto direction =
        rotation * glm::vec3(input.movement.x, 0, -input.movement.z) + up * input.movement.y;

    const float speed = s.movementSpeed * (input.sprint ? s.sprintMultiplier : 1.f);
    camera.transform.position += glm::normalize(direction) * amount * speed * dt;
}
} // namespace rt
