#pragma once

#include "input/KeyCode.h"
#include "math/MathTypes.h"

class IInputHandler {
public:
    virtual ~IInputHandler() = default;

    virtual bool isKeyPressed(KeyCode key) const = 0;
    virtual bool isMouseButtonPressed(KeyCode button) const = 0;
    virtual Vec2 getMousePosition() const = 0;
};
