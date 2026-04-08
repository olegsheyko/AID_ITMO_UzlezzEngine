#include "input/GlfwInputHandler.h"

#include <GLFW/glfw3.h>

GlfwInputHandler::GlfwInputHandler(GLFWwindow* window)
    : window_(window) {
}

bool GlfwInputHandler::isKeyPressed(KeyCode key) const {
    const int glfwKey = toGlfwKey(key);
    return window_ != nullptr && glfwKey != GLFW_KEY_UNKNOWN && glfwGetKey(window_, glfwKey) == GLFW_PRESS;
}

bool GlfwInputHandler::isMouseButtonPressed(KeyCode button) const {
    const int glfwButton = toGlfwMouseButton(button);
    return window_ != nullptr && glfwButton >= 0 && glfwGetMouseButton(window_, glfwButton) == GLFW_PRESS;
}

Vec2 GlfwInputHandler::getMousePosition() const {
    if (window_ == nullptr) {
        return Vec2{};
    }

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(window_, &mouseX, &mouseY);
    return Vec2{static_cast<float>(mouseX), static_cast<float>(mouseY)};
}

int GlfwInputHandler::toGlfwKey(KeyCode key) {
    switch (key) {
    case KeyCode::Enter:
        return GLFW_KEY_ENTER;
    case KeyCode::F3:
        return GLFW_KEY_F3;
    case KeyCode::Space:
        return GLFW_KEY_SPACE;
    case KeyCode::Left:
        return GLFW_KEY_LEFT;
    case KeyCode::Right:
        return GLFW_KEY_RIGHT;
    case KeyCode::Up:
        return GLFW_KEY_UP;
    case KeyCode::Down:
        return GLFW_KEY_DOWN;
    case KeyCode::W:
        return GLFW_KEY_W;
    case KeyCode::A:
        return GLFW_KEY_A;
    case KeyCode::S:
        return GLFW_KEY_S;
    case KeyCode::D:
        return GLFW_KEY_D;
    case KeyCode::I:
        return GLFW_KEY_I;
    case KeyCode::J:
        return GLFW_KEY_J;
    case KeyCode::K:
        return GLFW_KEY_K;
    case KeyCode::L:
        return GLFW_KEY_L;
    case KeyCode::U:
        return GLFW_KEY_U;
    case KeyCode::O:
        return GLFW_KEY_O;
    default:
        return GLFW_KEY_UNKNOWN;
    }
}

int GlfwInputHandler::toGlfwMouseButton(KeyCode button) {
    switch (button) {
    case KeyCode::MouseLeft:
        return GLFW_MOUSE_BUTTON_LEFT;
    case KeyCode::MouseRight:
        return GLFW_MOUSE_BUTTON_RIGHT;
    case KeyCode::MouseMiddle:
        return GLFW_MOUSE_BUTTON_MIDDLE;
    default:
        return -1;
    }
}
