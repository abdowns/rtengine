#pragma once

#include "scene.hpp"

namespace rt {
struct CameraControllerSettings {
    float movementSpeed = 5.f; // meters per second
    float sprintMultiplier = 3.f;
    float mouseSensitivity = .0025f; // radians per pixel
    float maxPitch = glm::radians(89.f);
    float maxFrameSeconds = .1f; // cap movement after stalls
    bool invertY = false;
    bool worldUpMovement = true; // world y for vertical movement
};

struct CameraInput {
    glm::vec3 movement{0};   // x right y up z forward
    glm::vec2 mouseDelta{0}; // pixels x right y down
    bool sprint = false;
};

// fly camera with caller supplied input
class CameraController {
  public:
    CameraControllerSettings settings;
    explicit CameraController(const CameraControllerSettings& = {});
    void update(Camera&, const CameraInput&, float frameSeconds) const;
};
} // namespace rt
