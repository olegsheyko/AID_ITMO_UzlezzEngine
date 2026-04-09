#include "input/InputManager.h"

#include <algorithm>

namespace {
constexpr KeyCode kTrackedKeys[] = {
    KeyCode::Enter,
    KeyCode::F3,
    KeyCode::Space,
    KeyCode::Left,
    KeyCode::Right,
    KeyCode::Up,
    KeyCode::Down,
    KeyCode::W,
    KeyCode::A,
    KeyCode::S,
    KeyCode::D,
    KeyCode::I,
    KeyCode::J,
    KeyCode::K,
    KeyCode::L,
    KeyCode::U,
    KeyCode::O
};

constexpr KeyCode kTrackedMouseButtons[] = {
    KeyCode::MouseLeft,
    KeyCode::MouseRight,
    KeyCode::MouseMiddle
};
}

InputManager& InputManager::getInstance() {
    static InputManager instance;
    return instance;
}

void InputManager::initialize(std::unique_ptr<IInputHandler> handler) {
    handler_ = std::move(handler);
    keyStates_.clear();
    mouseButtonStates_.clear();
    trackedKeys_.clear();
    trackedMouseButtons_.clear();

    for (KeyCode key : kTrackedKeys) {
        trackKey(key);
    }
    for (KeyCode button : kTrackedMouseButtons) {
        trackMouseButton(button);
    }

    mousePosition_ = Vec2{};
    previousMousePosition_ = Vec2{};
    mouseDelta_ = Vec2{};
    firstMouseSample_ = true;
}

void InputManager::shutdown() {
    handler_.reset();
    keyStates_.clear();
    mouseButtonStates_.clear();
    actionBindings_.clear();
    trackedKeys_.clear();
    trackedMouseButtons_.clear();
    mousePosition_ = Vec2{};
    previousMousePosition_ = Vec2{};
    mouseDelta_ = Vec2{};
    firstMouseSample_ = true;
}

void InputManager::update() {
    if (!handler_) {
        return;
    }

    for (KeyCode key : trackedKeys_) {
        ButtonState& state = keyStates_[key];
        state.previous = state.current;
        state.current = handler_->isKeyPressed(key);
    }

    for (KeyCode button : trackedMouseButtons_) {
        ButtonState& state = mouseButtonStates_[button];
        state.previous = state.current;
        state.current = handler_->isMouseButtonPressed(button);
    }

    mousePosition_ = handler_->getMousePosition();

    if (firstMouseSample_) {
        previousMousePosition_ = mousePosition_;
        firstMouseSample_ = false;
    }

    mouseDelta_ = Vec2{
        mousePosition_.x - previousMousePosition_.x,
        mousePosition_.y - previousMousePosition_.y
    };
    previousMousePosition_ = mousePosition_;
}

bool InputManager::isKeyDown(KeyCode key) const {
    const ButtonState state = getKeyState(key);
    return state.current;
}

bool InputManager::isKeyPressed(KeyCode key) const {
    const ButtonState state = getKeyState(key);
    return state.current && !state.previous;
}

bool InputManager::isKeyReleased(KeyCode key) const {
    const ButtonState state = getKeyState(key);
    return !state.current && state.previous;
}

bool InputManager::isMouseButtonDown(KeyCode button) const {
    const ButtonState state = getMouseButtonState(button);
    return state.current;
}

bool InputManager::isMouseButtonPressed(KeyCode button) const {
    const ButtonState state = getMouseButtonState(button);
    return state.current && !state.previous;
}

bool InputManager::isMouseButtonReleased(KeyCode button) const {
    const ButtonState state = getMouseButtonState(button);
    return !state.current && state.previous;
}

void InputManager::bindAction(const std::string& actionName, KeyCode key) {
    std::vector<KeyCode>& bindings = actionBindings_[actionName];
    if (std::find(bindings.begin(), bindings.end(), key) == bindings.end()) {
        bindings.push_back(key);
    }

    switch (key) {
    case KeyCode::MouseLeft:
    case KeyCode::MouseRight:
    case KeyCode::MouseMiddle:
        trackMouseButton(key);
        break;
    default:
        trackKey(key);
        break;
    }
}

bool InputManager::isActionDown(const std::string& actionName) const {
    return evaluateAction(actionName, false, false);
}

bool InputManager::isActionPressed(const std::string& actionName) const {
    return evaluateAction(actionName, true, false);
}

bool InputManager::isActionReleased(const std::string& actionName) const {
    return evaluateAction(actionName, false, true);
}

bool InputManager::evaluateAction(const std::string& actionName, bool pressedOnly, bool releasedOnly) const {
    auto it = actionBindings_.find(actionName);
    if (it == actionBindings_.end()) {
        return false;
    }

    for (KeyCode key : it->second) {
        if (pressedOnly) {
            if (key == KeyCode::MouseLeft || key == KeyCode::MouseRight || key == KeyCode::MouseMiddle) {
                if (isMouseButtonPressed(key)) {
                    return true;
                }
            } else if (isKeyPressed(key)) {
                return true;
            }
        } else if (releasedOnly) {
            if (key == KeyCode::MouseLeft || key == KeyCode::MouseRight || key == KeyCode::MouseMiddle) {
                if (isMouseButtonReleased(key)) {
                    return true;
                }
            } else if (isKeyReleased(key)) {
                return true;
            }
        } else {
            if (key == KeyCode::MouseLeft || key == KeyCode::MouseRight || key == KeyCode::MouseMiddle) {
                if (isMouseButtonDown(key)) {
                    return true;
                }
            } else if (isKeyDown(key)) {
                return true;
            }
        }
    }

    return false;
}

InputManager::ButtonState InputManager::getKeyState(KeyCode key) const {
    auto it = keyStates_.find(key);
    return it == keyStates_.end() ? ButtonState{} : it->second;
}

InputManager::ButtonState InputManager::getMouseButtonState(KeyCode button) const {
    auto it = mouseButtonStates_.find(button);
    return it == mouseButtonStates_.end() ? ButtonState{} : it->second;
}

void InputManager::trackKey(KeyCode key) {
    trackedKeys_.insert(key);
    keyStates_.try_emplace(key, ButtonState{});
}

void InputManager::trackMouseButton(KeyCode button) {
    trackedMouseButtons_.insert(button);
    mouseButtonStates_.try_emplace(button, ButtonState{});
}
