#include "camera_input.hpp"

GlfwCameraInput::~GlfwCameraInput() {
    if (captured_)
        glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

rt::CameraInput GlfwCameraInput::poll(bool keyboardAllowed, bool mouseAllowed) {
    rt::CameraInput input;
    const bool focused = glfwGetWindowAttrib(window_, GLFW_FOCUSED) == GLFW_TRUE;
    const bool capture =
        focused && mouseAllowed && glfwGetMouseButton(window_, bindings.lookButton) == GLFW_PRESS;
    double x, y;

    if (capture != captured_) {
        captured_ = capture;
        glfwSetInputMode(window_, GLFW_CURSOR, capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        // skip capture delta to avoid jumps
        glfwGetCursorPos(window_, &lastX_, &lastY_);
    }

    glfwGetCursorPos(window_, &x, &y);

    if (captured_)
        input.mouseDelta = {float(x - lastX_), float(y - lastY_)};
    lastX_ = x;
    lastY_ = y;

    if (focused && keyboardAllowed) {
        auto pressed = [&](int key) { return glfwGetKey(window_, key) == GLFW_PRESS ? 1.f : 0.f; };
        input.movement = {pressed(bindings.right) - pressed(bindings.left),
                          pressed(bindings.up) - pressed(bindings.down),
                          pressed(bindings.forward) - pressed(bindings.backward)};
        input.sprint = pressed(bindings.sprint) != 0;
    }

    return input;
}
