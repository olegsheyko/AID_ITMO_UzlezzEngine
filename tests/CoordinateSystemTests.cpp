#include "editor/EditorCamera.h"
#include "ecs/PhysicsSystem.h"
#include "ecs/SpinSystem.h"
#include "ecs/World.h"
#include "input/InputManager.h"
#include "math/CameraMath.h"
#include "resources/SceneManifest.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_set>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b) { return std::isfinite(a) && std::abs(a - b) < 0.0001f; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 project(const Mat4& m, Vec3 p) {
    const float w = m.values[3] * p.x + m.values[7] * p.y + m.values[11] * p.z + m.values[15];
    return {(m.values[0] * p.x + m.values[4] * p.y + m.values[8] * p.z + m.values[12]) / w,
        (m.values[1] * p.x + m.values[5] * p.y + m.values[9] * p.z + m.values[13]) / w,
        (m.values[2] * p.x + m.values[6] * p.y + m.values[10] * p.z + m.values[14]) / w};
}

class TestInput : public IInputHandler {
public:
    std::unordered_set<KeyCode> keys;
    Vec2 mouse{};
    bool rightMouse = true;
    bool isKeyPressed(KeyCode key) const override { return keys.count(key) != 0; }
    bool isMouseButtonPressed(KeyCode key) const override { return rightMouse && key == KeyCode::MouseRight; }
    Vec2 getMousePosition() const override { return mouse; }
};

void testCamera() {
    for (float yaw : {0.0f, 0.7f, -1.8f, 3.14f}) {
        for (float pitch : {0.0f, -0.4f, 1.48f}) {
            const Vec3 f = CameraMath::forward(pitch, yaw);
            const Vec3 r = CameraMath::right(yaw);
            const Vec3 u = CameraMath::up(pitch, yaw);
            require(near(dot(f, f), 1) && near(dot(r, r), 1) && near(dot(u, u), 1), "Camera basis is not normalized");
            require(near(dot(f, r), 0) && near(dot(f, u), 0) && near(dot(r, u), 0), "Camera basis is not orthogonal");
            require(near(dot(cross(r, u), f), -1), "View basis reflects the world");
        }
    }
    const auto vp = Math::multiply(Math::perspective(0.8f, 1.5f, 0.1f, 100.0f),
        CameraMath::view(Vec3{0, -8, 0}, 0, 0));
    require(project(vp, Vec3{1, 0, 0}).x > 0, "+X should appear on the right in front view");
    require(project(vp, Vec3{0, 0, 1}).y > 0, "+Z should appear above in front view");
    require(project(vp, Vec3{0, -1, 0}).z < project(vp, Vec3{0, 1, 0}).z, "-Y should be the front side");

    auto handler = std::make_unique<TestInput>();
    auto* input = handler.get();
    auto& manager = InputManager::getInstance();
    manager.initialize(std::move(handler));
    EditorCamera camera;
    auto step = [&](KeyCode key) {
        input->keys = {key};
        manager.update();
        camera.update(0.2f, 900, 600, true, 0);
    };
    Vec3 before = camera.getPosition();
    step(KeyCode::E);
    require(near(camera.getPosition().z, before.z + 1) && near(camera.getPosition().y, before.y), "E must move along +Z");
    step(KeyCode::Q);
    require(near(camera.getPosition().z, before.z), "Q must move along -Z");
    step(KeyCode::D);
    require(near(camera.getPosition().x, before.x + 1), "D must move to camera right");
    before = camera.getPosition();
    step(KeyCode::W);
    require(near(camera.getPosition().y, before.y + 1) && near(camera.getPosition().z, before.z), "W must move along the view in the XY plane");
    input->keys.clear();
    input->mouse = {100, -50};
    manager.update();
    const auto oldForward = camera.getForward();
    camera.update(0, 900, 600, true, 0);
    require(camera.getForward().x > oldForward.x && camera.getForward().z > oldForward.z, "Mouse right/up must look right/up");
    require(near(dot(camera.getRayDirection(0, 0, 1.5f), camera.getForward()), 1), "Picking center ray disagrees with camera");
    const auto ray = camera.getRayDirection(0.5f, 0.4f, 1.5f);
    const Vec3 pos = camera.getPosition();
    const Vec3 point{pos.x + ray.x * 5, pos.y + ray.y * 5, pos.z + ray.z * 5};
    const Vec3 screen = project(Math::multiply(camera.getProjectionMatrix(), camera.getViewMatrix()), point);
    require(near(screen.x, 0.5f) && near(screen.y, 0.4f), "Picking ray does not match displayed coordinates");
    camera.focus(Vec3{2, 3, 4}, 1);
    camera.update(0, 900, 600, false, 0);
    const Vec3 centered = project(Math::multiply(camera.getProjectionMatrix(), camera.getViewMatrix()), Vec3{2, 3, 4});
    require(near(centered.x, 0) && near(centered.y, 0), "Focus no longer centers the object");
    manager.shutdown();
}

void testPhysicsAndScene() {
    World world;
    const auto body = world.createEntity();
    world.addComponent<Transform>(body).position = Vec3{1, 2, 3};
    world.addComponent<Rigidbody>(body);
    PhysicsSystem physics;
    physics.update(world, 0.1f);
    const auto velocity = world.getComponent<Rigidbody>(body).velocity;
    require(near(velocity.x, 0) && near(velocity.y, 0) && velocity.z < 0, "Gravity must act only on -Z");
    world.addComponent<Spin>(body).speed = 2;
    SpinSystem spin;
    spin.update(world, 0.5f);
    const auto rotation = world.getComponent<Transform>(body).rotation;
    require(near(rotation.x, 0) && near(rotation.y, 0) && near(rotation.z, 1), "Spin must rotate around Z");

    SceneManifest manifest;
    require(manifest.loadFromFile("assets/scenes/demo_scene.json"), "Demo scene could not load");
    bool foundGround = false, foundFalling = false;
    for (const auto& entity : manifest.getEntities()) {
        if (entity.tag == "Ground") {
            foundGround = true;
            require(entity.position.z < 0 && entity.scale.x > entity.scale.z && entity.scale.y > entity.scale.z, "Floor must lie in XY below the origin");
        }
        if (entity.tag == "FallingCubeA") {
            foundFalling = true;
            require(entity.position.z > 2 && entity.hasRigidbody && entity.rigidbody.useGravity, "Falling cube must start above the floor");
        }
    }
    require(foundGround && foundFalling, "Demo scene fixtures missing");
}
}

int main() {
    try {
        testCamera();
        testPhysicsAndScene();
        std::cout << "PASS: Z-up camera, movement, mouse look, picking, focus, gravity, spin and scene\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
