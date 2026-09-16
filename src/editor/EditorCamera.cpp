#include "editor/EditorCamera.h"

#include "input/InputManager.h"
#include "input/KeyCode.h"
#include "math/CameraMath.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.1415926f;
constexpr float kLookSensitivity = 0.003f;
constexpr float kOrbitSensitivity = 0.005f;
constexpr float kPanSensitivity = 0.004f;
constexpr float kBaseMoveSpeed = 5.0f;
constexpr float kFastMoveMultiplier = 3.0f;
constexpr float kZoomSpeed = 0.18f;
constexpr float kMinDistance = 0.5f;
constexpr float kMaxPitch = 1.48f;

Vec3 add(const Vec3& left, const Vec3& right) {
    return Vec3{left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 subtract(const Vec3& left, const Vec3& right) {
    return Vec3{left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 scale(const Vec3& value, float scalar) {
    return Vec3{value.x * scalar, value.y * scalar, value.z * scalar};
}

float length(const Vec3& value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

Vec3 normalize(const Vec3& value) {
    const float valueLength = length(value);
    if (valueLength <= 0.0001f) {
        return Vec3{};
    }

    return scale(value, 1.0f / valueLength);
}
}

void EditorCamera::update(float dt, int viewportWidth, int viewportHeight, bool inputEnabled, float mouseWheelDelta) {
    if (inputEnabled) {
        InputManager& input = InputManager::getInstance();
        const Vec2 mouseDelta = input.getMouseDelta();
        const bool rmb = input.isMouseButtonDown(KeyCode::MouseRight);
        const bool mmb = input.isMouseButtonDown(KeyCode::MouseMiddle);
        const bool alt = input.isKeyDown(KeyCode::LeftAlt);
        const bool shift = input.isKeyDown(KeyCode::LeftShift);

        if (rmb) {
            yaw_ -= mouseDelta.x * kLookSensitivity;
            pitch_ -= mouseDelta.y * kLookSensitivity;
            pitch_ = std::clamp(pitch_, -kMaxPitch, kMaxPitch);

            float moveForward = 0.0f;
            float moveRight = 0.0f;
            float moveUp = 0.0f;
            if (input.isKeyDown(KeyCode::W)) moveForward += 1.0f;
            if (input.isKeyDown(KeyCode::S)) moveForward -= 1.0f;
            if (input.isKeyDown(KeyCode::D)) moveRight += 1.0f;
            if (input.isKeyDown(KeyCode::A)) moveRight -= 1.0f;
            if (input.isKeyDown(KeyCode::E)) moveUp += 1.0f;
            if (input.isKeyDown(KeyCode::Q)) moveUp -= 1.0f;

            const float speed = kBaseMoveSpeed * (shift ? kFastMoveMultiplier : 1.0f);
            Vec3 flatForward = getForward();
            flatForward.z = 0.0f;
            flatForward = normalize(flatForward);
            if (length(flatForward) <= 0.0001f) {
                flatForward = Vec3{0.0f, 1.0f, 0.0f};
            }

            Vec3 movement{};
            movement = add(movement, scale(flatForward, moveForward * speed * dt));
            movement = add(movement, scale(getRight(), moveRight * speed * dt));
            movement = add(movement, Vec3{0.0f, 0.0f, moveUp * speed * dt});
            position_ = add(position_, movement);
            pivot_ = add(pivot_, movement);
            distance_ = std::max(kMinDistance, std::sqrt(
                std::pow(position_.x - pivot_.x, 2.0f) +
                std::pow(position_.y - pivot_.y, 2.0f) +
                std::pow(position_.z - pivot_.z, 2.0f)));
        } else if (alt && input.isMouseButtonDown(KeyCode::MouseLeft)) {
            yaw_ -= mouseDelta.x * kOrbitSensitivity;
            pitch_ -= mouseDelta.y * kOrbitSensitivity;
            pitch_ = std::clamp(pitch_, -kMaxPitch, kMaxPitch);
            position_ = subtract(pivot_, scale(getForward(), distance_));
        } else if (mmb) {
            const float panScale = std::max(distance_, 1.0f) * kPanSensitivity;
            Vec3 pan{};
            pan = add(pan, scale(getRight(), -mouseDelta.x * panScale));
            pan = add(pan, scale(getUp(), mouseDelta.y * panScale));
            position_ = add(position_, pan);
            pivot_ = add(pivot_, pan);
        }

        if (std::abs(mouseWheelDelta) > 0.0001f) {
            const float amount = std::max(distance_, 1.0f) * kZoomSpeed * mouseWheelDelta;
            const Vec3 delta = scale(getForward(), amount);
            position_ = add(position_, delta);
            distance_ = std::max(kMinDistance, distance_ - amount);
        }

    }

    updateMatrices(viewportWidth, viewportHeight);
}

void EditorCamera::focus(const Vec3& target, float radius) {
    pivot_ = target;
    distance_ = std::max(radius * 2.5f, 2.0f);
    position_ = subtract(pivot_, scale(getForward(), distance_));
}

void EditorCamera::updateMatrices(int viewportWidth, int viewportHeight) {
    viewMatrix_ = CameraMath::view(position_, pitch_, yaw_);

    const float aspect = (viewportWidth > 0 && viewportHeight > 0)
        ? static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight)
        : 800.0f / 600.0f;
    projectionMatrix_ = Math::perspective(fovDegrees_ * kPi / 180.0f, aspect, nearClip_, farClip_);
}

Vec3 EditorCamera::getForward() const {
    return CameraMath::forward(pitch_, yaw_);
}

Vec3 EditorCamera::getRight() const {
    return CameraMath::right(yaw_);
}

Vec3 EditorCamera::getUp() const {
    return CameraMath::up(pitch_, yaw_);
}

Vec3 EditorCamera::getRayDirection(float normalizedX, float normalizedY, float aspect) const {
    const float tanHalfFov = std::tan((fovDegrees_ * kPi / 180.0f) * 0.5f);
    Vec3 direction = getForward();
    direction = add(direction, scale(getRight(), normalizedX * aspect * tanHalfFov));
    direction = add(direction, scale(getUp(), normalizedY * tanHalfFov));
    return normalize(direction);
}
