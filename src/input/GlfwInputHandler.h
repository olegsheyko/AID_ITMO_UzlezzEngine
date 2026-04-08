#pragma once

#include "input/IInputHandler.h"

struct GLFWwindow;

class GlfwInputHandler : public IInputHandler {
public:
    explicit GlfwInputHandler(GLFWwindow* window);

    bool isKeyPressed(KeyCode key) const override;
    bool isMouseButtonPressed(KeyCode button) const override;
    Vec2 getMousePosition() const override;

private:
    static int toGlfwKey(KeyCode key);
    static int toGlfwMouseButton(KeyCode button);

    GLFWwindow* window_ = nullptr;
};
