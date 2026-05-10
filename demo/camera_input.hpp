#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <rtengine/camera_controller.hpp>

struct CameraBindings {
    int forward = GLFW_KEY_W, backward = GLFW_KEY_S;
    int left = GLFW_KEY_A, right = GLFW_KEY_D;
    int down = GLFW_KEY_Q, up = GLFW_KEY_E;
    int sprint = GLFW_KEY_LEFT_SHIFT, lookButton = GLFW_MOUSE_BUTTON_RIGHT;
};

// glfw camera input and cursor capture
class GlfwCameraInput {
  public:
    CameraBindings bindings;

    explicit GlfwCameraInput(GLFWwindow* window) : window_(window) {}
    ~GlfwCameraInput();
    GlfwCameraInput(const GlfwCameraInput&) = delete;
    GlfwCameraInput& operator=(const GlfwCameraInput&) = delete;
    rt::CameraInput poll(bool keyboardAllowed = true, bool mouseAllowed = true);

  private:
    GLFWwindow* window_;
    bool captured_ = false;
    double lastX_ = 0, lastY_ = 0;
};
