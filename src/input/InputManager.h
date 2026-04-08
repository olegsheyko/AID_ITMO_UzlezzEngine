#pragma once

#include "input/IInputHandler.h"
#include "input/KeyCode.h"
#include "math/MathTypes.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class InputManager final : public IInputHandler {
public:
    static InputManager& getInstance();

    void initialize(std::unique_ptr<IInputHandler> handler);
    void Initialize(std::unique_ptr<IInputHandler> handler) { initialize(std::move(handler)); }
    void shutdown();
    void Shutdown() { shutdown(); }
    void update();
    void updateState() { update(); }
    void UpdateState() { updateState(); }

    bool isKeyDown(KeyCode key) const;
    bool isKeyPressed(KeyCode key) const override;
    bool isKeyReleased(KeyCode key) const;
    bool IsKeyPressed(KeyCode key) const { return isKeyPressed(key); }

    bool isMouseButtonDown(KeyCode button) const;
    bool isMouseButtonPressed(KeyCode button) const override;
    bool isMouseButtonReleased(KeyCode button) const;
    bool IsMouseButtonPressed(KeyCode button) const { return isMouseButtonPressed(button); }

    Vec2 getMousePosition() const override { return mousePosition_; }
    Vec2 GetMousePosition() const { return getMousePosition(); }
    Vec2 getMouseDelta() const { return mouseDelta_; }
    Vec2 GetMouseDelta() const { return getMouseDelta(); }

    void bindAction(const std::string& actionName, KeyCode key);
    void BindAction(const std::string& actionName, KeyCode key) { bindAction(actionName, key); }
    bool isActionDown(const std::string& actionName) const;
    bool isActionPressed(const std::string& actionName) const;
    bool isActionReleased(const std::string& actionName) const;
    bool isActionActive(const std::string& actionName) const { return isActionDown(actionName); }
    bool IsActionActive(const std::string& actionName) const { return isActionActive(actionName); }

private:
    struct ButtonState {
        bool current = false;
        bool previous = false;
    };

    bool evaluateAction(const std::string& actionName, bool pressedOnly, bool releasedOnly) const;
    ButtonState getKeyState(int key) const;
    ButtonState getKeyState(KeyCode key) const;
    ButtonState getMouseButtonState(KeyCode button) const;
    void trackKey(KeyCode key);
    void trackMouseButton(KeyCode button);

    std::unique_ptr<IInputHandler> handler_;
    std::unordered_map<KeyCode, ButtonState> keyStates_;
    std::unordered_map<KeyCode, ButtonState> mouseButtonStates_;
    std::unordered_map<std::string, std::vector<KeyCode>> actionBindings_;
    std::unordered_set<KeyCode> trackedKeys_;
    std::unordered_set<KeyCode> trackedMouseButtons_;
    Vec2 mousePosition_{};
    Vec2 previousMousePosition_{};
    Vec2 mouseDelta_{};
    bool firstMouseSample_ = true;
};
