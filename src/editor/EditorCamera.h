#pragma once

#include "math/MathTypes.h"

class InputManager;

class EditorCamera {
public:
    void update(float dt, int viewportWidth, int viewportHeight, bool inputEnabled, float mouseWheelDelta);
    void focus(const Vec3& target, float radius);
    // Плавно разворачивает камеру вокруг точки опоры, чтобы она смотрела вдоль direction.
    void lookAlong(const Vec3& direction);
    float getYaw() const { return yaw_; }
    float getPitch() const { return pitch_; }
    const Vec3& getPivot() const { return pivot_; }
    float getFov() const { return fovDegrees_; }
    void setFov(float degrees) { fovDegrees_ = degrees; }

    // Скорость полёта на RMB+WASD, единиц в секунду.
    float moveSpeed = 5.0f;

    const Mat4& getViewMatrix() const { return viewMatrix_; }
    const Mat4& getProjectionMatrix() const { return projectionMatrix_; }
    const Vec3& getPosition() const { return position_; }
    Vec3 getForward() const;
    Vec3 getRight() const;
    Vec3 getUp() const;
    Vec3 getRayDirection(float normalizedX, float normalizedY, float aspect) const;

private:
    void updateMatrices(int viewportWidth, int viewportHeight);

    Vec3 position_{0.0f, -8.0f, 3.5f};
    Vec3 pivot_{0.0f, 0.0f, 0.0f};
    float yaw_ = 0.0f;
    float pitch_ = -0.35f;
    float distance_ = 8.0f;
    float fovDegrees_ = 45.0f;
    float nearClip_ = 0.1f;
    float farClip_ = 200.0f;
    bool animating_ = false;
    float animationTime_ = 0.0f;
    float startYaw_ = 0.0f;
    float startPitch_ = 0.0f;
    float targetYaw_ = 0.0f;
    float targetPitch_ = 0.0f;
    Mat4 viewMatrix_ = Mat4::identity();
    Mat4 projectionMatrix_ = Mat4::identity();
};
