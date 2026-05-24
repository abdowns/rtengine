#include <algorithm>
#include <cmath>
#include <rtengine/camera_controller.hpp>
#include <stdexcept>

namespace rt {
CameraController::CameraController(const CameraControllerSettings& s) : settings(s) {}

void CameraController::update(Camera& camera, const CameraInput& input, float frameSeconds) const {
    const auto& s = settings;

    if (!std::isfinite(frameSeconds) || frameSeconds < 0 || !std::isfinite(s.movementSpeed) ||
        s.movementSpeed < 0 || !std::isfinite(s.sprintMultiplier) || s.sprintMultiplier < 1 ||
        !std::isfinite(s.mouseSensitivity) || s.mouseSensitivity < 0 ||
        !std::isfinite(s.maxPitch) || s.maxPitch <= 0 || s.maxPitch >= glm::radians(90.f) ||
        !std::isfinite(s.maxFrameSeconds) || s.maxFrameSeconds <= 0)
        throw std::invalid_argument("Invalid camera controller settings or frame duration");

    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(input.movement[i]))
            throw std::invalid_argument("Camera movement must be finite");

    if (!std::isfinite(input.mouseDelta.x) || !std::isfinite(input.mouseDelta.y))
        throw std::invalid_argument("Mouse delta must be finite");

    const float rotationLength = glm::length(camera.transform.rotation);

    if (!std::isfinite(rotationLength) || rotationLength < 1e-6f)
        throw std::invalid_argument("Camera rotation must be a valid quaternion");

    auto rotation = glm::normalize(camera.transform.rotation);

    if (input.mouseDelta != glm::vec2(0)) {
        // use current pose so external edits work
        const auto forward = rotation * glm::vec3(0, 0, -1);
        float yaw = std::atan2(-forward.x, -forward.z);
        float pitch = std::asin(std::clamp(forward.y, -1.f, 1.f));
        yaw -= input.mouseDelta.x * s.mouseSensitivity;
        pitch += input.mouseDelta.y * s.mouseSensitivity * (s.invertY ? 1.f : -1.f);
        pitch = std::clamp(pitch, -s.maxPitch, s.maxPitch);
        rotation = glm::normalize(glm::angleAxis(yaw, glm::vec3(0, 1, 0)) *
                                  glm::angleAxis(pitch, glm::vec3(1, 0, 0)));
        camera.transform.rotation = rotation;
    }

    const float amount = std::min(glm::length(input.movement), 1.f);

    if (amount == 0)
        return;

    auto direction = rotation * glm::vec3(input.movement.x, 0, -input.movement.z);
    direction +=
        (s.worldUpMovement ? glm::vec3(0, 1, 0) : rotation * glm::vec3(0, 1, 0)) * input.movement.y;

    if (glm::length(direction) < 1e-6f)
        return;

    const float speed = s.movementSpeed * (input.sprint ? s.sprintMultiplier : 1.f);
    camera.transform.position +=
        glm::normalize(direction) * amount * speed * std::min(frameSeconds, s.maxFrameSeconds);
}
} // namespace rt
