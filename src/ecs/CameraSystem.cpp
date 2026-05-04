#include "ecs/CameraSystem.h"

#include "ecs/Components.h"
#include "ecs/World.h"
#include "input/InputManager.h"
#include "render/IRenderAdapter.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kCameraMoveSpeed = 3.5f;
constexpr float kCameraLookSensitivity = 0.003f;
constexpr float kMaxCameraPitch = 1.4f;

Mat4 buildViewMatrix(const Transform& transform) {
    const float cosPitch = std::cos(transform.rotation.x);
    const Vec3 forward{
        std::sin(transform.rotation.y) * cosPitch,
        std::sin(transform.rotation.x),
        -std::cos(transform.rotation.y) * cosPitch
    };
    const Vec3 right{std::cos(transform.rotation.y), 0.0f, std::sin(transform.rotation.y)};
    const Vec3 up{
        right.y * forward.z - right.z * forward.y,
        right.z * forward.x - right.x * forward.z,
        right.x * forward.y - right.y * forward.x
    };

    Mat4 view = Mat4::identity();
    view.values[0] = right.x;
    view.values[1] = up.x;
    view.values[2] = -forward.x;
    view.values[4] = right.y;
    view.values[5] = up.y;
    view.values[6] = -forward.y;
    view.values[8] = right.z;
    view.values[9] = up.z;
    view.values[10] = -forward.z;
    view.values[12] = -(right.x * transform.position.x + right.y * transform.position.y + right.z * transform.position.z);
    view.values[13] = -(up.x * transform.position.x + up.y * transform.position.y + up.z * transform.position.z);
    view.values[14] = forward.x * transform.position.x + forward.y * transform.position.y + forward.z * transform.position.z;
    return view;
}
}

CameraSystem::CameraSystem(IRenderAdapter& renderer)
    : renderer_(renderer) {
}

void CameraSystem::update(World& world, float dt) {
    InputManager& inputManager = InputManager::getInstance();

    world.forEach<Transform, Camera>([&](Entity, Transform& transform, Camera& camera) {
        if (!camera.active) {
            return;
        }

        float moveForward = 0.0f;
        float moveRight = 0.0f;
        float moveUp = 0.0f;

        if (inputManager.IsActionActive("CameraForward")) {
            moveForward += 1.0f;
        }
        if (inputManager.IsActionActive("CameraBackward")) {
            moveForward -= 1.0f;
        }
        if (inputManager.IsActionActive("CameraRight")) {
            moveRight += 1.0f;
        }
        if (inputManager.IsActionActive("CameraLeft")) {
            moveRight -= 1.0f;
        }
        if (inputManager.IsActionActive("CameraUp")) {
            moveUp += 1.0f;
        }
        if (inputManager.IsActionActive("CameraDown")) {
            moveUp -= 1.0f;
        }

        const float yaw = transform.rotation.y;
        const float forwardX = std::sin(yaw);
        const float forwardZ = -std::cos(yaw);
        const float rightX = std::cos(yaw);
        const float rightZ = std::sin(yaw);

        transform.position.x += (forwardX * moveForward + rightX * moveRight) * kCameraMoveSpeed * dt;
        transform.position.y += moveUp * kCameraMoveSpeed * dt;
        transform.position.z += (forwardZ * moveForward + rightZ * moveRight) * kCameraMoveSpeed * dt;

        if (inputManager.isMouseButtonDown(KeyCode::MouseRight)) {
            const Vec2 mouseDelta = inputManager.getMouseDelta();
            transform.rotation.y += mouseDelta.x * kCameraLookSensitivity;
            transform.rotation.x -= mouseDelta.y * kCameraLookSensitivity;
            transform.rotation.x = std::clamp(transform.rotation.x, -kMaxCameraPitch, kMaxCameraPitch);
        }

        int width = 0;
        int height = 0;
        renderer_.getFramebufferSize(width, height);
        camera.aspectRatio = (width > 0 && height > 0) ? static_cast<float>(width) / static_cast<float>(height) : (800.0f / 600.0f);
        camera.viewMatrix = buildViewMatrix(transform);
        camera.projectionMatrix = Math::perspective(
            camera.fovDegrees * 3.1415926f / 180.0f,
            camera.aspectRatio,
            camera.nearClip,
            camera.farClip);
    });
}
