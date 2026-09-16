#include "core/ServiceLocator.h"
#include "ecs/Components.h"
#include "ecs/PhysicsSystem.h"
#include "ecs/World.h"
#include "events/CollisionEvent.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr float kTolerance = 0.0001f;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool near(float actual, float expected) {
    return std::isfinite(actual) && std::fabs(actual - expected) < kTolerance;
}

float distance(const Vec3& a, const Vec3& b) {
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    const float z = a.z - b.z;
    return std::sqrt(x * x + y * y + z * z);
}

Entity addCollider(World& world, ColliderType type, Vec3 position, bool dynamic) {
    const Entity entity = world.createEntity();
    world.addComponent<Transform>(entity).position = position;
    world.addComponent<Collider>(entity).type = type;
    if (dynamic) {
        world.addComponent<Rigidbody>(entity).useGravity = false;
    }
    return entity;
}

void fallingSphereHitsBox(bool sphereCreatedFirst) {
    World world;
    Entity sphere;
    Entity ground;
    if (sphereCreatedFirst) {
        sphere = addCollider(world, ColliderType::Sphere, Vec3{0.0f, 0.0f, 2.0f}, true);
        ground = addCollider(world, ColliderType::Box, Vec3{0.0f, 0.0f, -0.5f}, false);
    } else {
        ground = addCollider(world, ColliderType::Box, Vec3{0.0f, 0.0f, -0.5f}, false);
        sphere = addCollider(world, ColliderType::Sphere, Vec3{0.0f, 0.0f, 2.0f}, true);
    }
    world.getComponent<Transform>(ground).scale = Vec3{8.0f, 8.0f, 1.0f};
    world.getComponent<Rigidbody>(sphere).useGravity = true;
    world.getComponent<Transform>(sphere).scale = Vec3{0.8f, 0.8f, 0.8f};

    std::vector<CollisionEvent> events;
    ServiceLocator::getEventDispatcher().addListener<CollisionEvent>(
        [&events](const CollisionEvent& event) { events.push_back(event); });
    PhysicsSystem physics;
    for (int frame = 0; frame < 600; ++frame) {
        physics.update(world, 1.0f / 120.0f);
        require(world.getComponent<Transform>(sphere).position.z >= 0.4f - kTolerance,
            "Sphere fell through the box floor");
    }
    require(!events.empty(), "Sphere-box collision event was not emitted");
    for (const auto& event : events) {
        require(near(event.normal.z, event.first == sphere ? 1.0f : -1.0f),
            "Collision normal points in the wrong direction");
        require(event.penetration > 0.0f, "Collision penetration must be positive");
    }
    require(near(world.getComponent<Transform>(ground).position.z, -0.5f),
        "Static floor moved");
}

void dynamicBoxHitsStaticSphere() {
    World world;
    addCollider(world, ColliderType::Sphere, Vec3{}, false);
    const Entity box = addCollider(world, ColliderType::Box, Vec3{0.0f, 0.8f, 0.0f}, true);
    world.getComponent<Rigidbody>(box).velocity.y = -1.0f;
    PhysicsSystem physics(0.0f);
    physics.update(world, 0.01f);
    require(physics.getLastCollisionCount() == 1, "Box-sphere collision was missed");
    require(near(world.getComponent<Transform>(box).position.y, 1.0f), "Box was not separated from sphere");
    require(world.getComponent<Rigidbody>(box).velocity.y > 0.0f, "Box velocity was not reflected");
}

void sphereContactsBoxEdges() {
    for (const Vec3 position : {Vec3{0.8f, 0.8f, 0.0f}, Vec3{0.75f, 0.75f, 0.75f}}) {
        World world;
        addCollider(world, ColliderType::Box, Vec3{}, false);
        const Entity sphere = addCollider(world, ColliderType::Sphere, position, true);
        PhysicsSystem physics(0.0f);
        physics.update(world, 0.01f);
        require(physics.getLastCollisionCount() == 1, "Sphere missed box edge/corner");
        const Vec3 closest{0.5f, 0.5f, position.z == 0.0f ? 0.0f : 0.5f};
        const Vec3 resolved = world.getComponent<Transform>(sphere).position;
        require(near(distance(resolved, closest), 0.5f), "Sphere was not separated along edge/corner normal");
    }
}

void sphereDoesNotCollideAsABox() {
    World world;
    addCollider(world, ColliderType::Box, Vec3{}, false);
    const Entity sphere = addCollider(world, ColliderType::Sphere, Vec3{0.9f, 0.9f, 0.0f}, true);
    PhysicsSystem physics(0.0f);
    physics.update(world, 0.01f);
    require(physics.getLastCollisionCount() == 0, "Sphere bounding box caused a false contact at corner");
    require(near(world.getComponent<Transform>(sphere).position.x, 0.9f), "Separated sphere moved");
}

void sphereInsideBox() {
    for (const Vec3 position : {Vec3{}, Vec3{0.0f, 0.3f, 0.0f}, Vec3{-0.3f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.3f}, Vec3{0.0f, 0.5f, 0.0f}}) {
        World world;
        addCollider(world, ColliderType::Box, Vec3{}, false);
        const Entity sphere = addCollider(world, ColliderType::Sphere, position, true);
        PhysicsSystem physics(0.0f);
        physics.update(world, 0.01f);
        require(physics.getLastCollisionCount() == 1, "Sphere inside box was ignored");
        const Vec3 resolved = world.getComponent<Transform>(sphere).position;
        require(std::isfinite(resolved.x) && std::isfinite(resolved.y) && std::isfinite(resolved.z),
            "Sphere inside box produced invalid position");
        require(near(std::fabs(resolved.x), 1.0f) || near(std::fabs(resolved.y), 1.0f) || near(std::fabs(resolved.z), 1.0f),
            "Sphere was not pushed out of box");
        if (position.y > 0.0f) require(near(resolved.y, 1.0f), "Sphere did not leave by nearest face");
        if (position.x < 0.0f) require(near(resolved.x, -1.0f), "Sphere did not leave by nearest face");
        if (position.z > 0.0f) require(near(resolved.z, 1.0f), "Sphere did not leave by nearest face");
    }
}

void spherePairs() {
    for (bool coincident : {false, true}) {
        World world;
        const Entity left = addCollider(world, ColliderType::Sphere, Vec3{}, true);
        const Entity right = addCollider(world, ColliderType::Sphere, Vec3{coincident ? 0.0f : 0.8f, 0.0f, 0.0f}, true);
        PhysicsSystem physics(0.0f);
        physics.update(world, 0.01f);
        require(physics.getLastCollisionCount() == 1, "Sphere-sphere contact was missed");
        require(near(distance(world.getComponent<Transform>(left).position, world.getComponent<Transform>(right).position), 1.0f),
            "Spheres were not separated, or coincident centers produced NaN");
    }

    World world;
    addCollider(world, ColliderType::Sphere, Vec3{}, false);
    addCollider(world, ColliderType::Sphere, Vec3{0.8f, 0.8f, 0.0f}, true);
    PhysicsSystem physics(0.0f);
    physics.update(world, 0.01f);
    require(physics.getLastCollisionCount() == 0, "Separated spheres collided as boxes");
}

void sphereScaleAndOffset() {
    for (ColliderType supportType : {ColliderType::Box, ColliderType::Sphere}) {
        World world;
        addCollider(world, supportType, Vec3{}, false);
        const Entity sphere = addCollider(world, ColliderType::Sphere, Vec3{0.0f, 0.2f, 0.0f}, true);
        world.getComponent<Transform>(sphere).scale = Vec3{-2.0f, 1.0f, 0.5f};
        auto& collider = world.getComponent<Collider>(sphere);
        collider.radius = 0.3f;
        collider.offset = Vec3{0.0f, 0.25f, 0.0f};
        PhysicsSystem physics(0.0f);
        physics.update(world, 0.01f);
        require(physics.getLastCollisionCount() == 1, "Scaled/offset sphere contact was missed");
        require(near(world.getComponent<Transform>(sphere).position.y, 0.85f), "Sphere radius, scale or offset ignored");
    }
}

void boxCollisionStillWorks() {
    World world;
    addCollider(world, ColliderType::Box, Vec3{}, false);
    const Entity box = addCollider(world, ColliderType::Box, Vec3{0.0f, 0.8f, 0.0f}, true);
    PhysicsSystem physics(0.0f);
    physics.update(world, 0.01f);
    require(physics.getLastCollisionCount() == 1, "Box-box collision regressed");
    require(near(world.getComponent<Transform>(box).position.y, 1.0f), "Box-box separation regressed");
}
}

int main() {
    int failures = 0;
    auto run = [&failures](const char* name, auto test) {
        ServiceLocator::getEventDispatcher().clear();
        try {
            test();
            std::cout << "PASS: " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
        }
        ServiceLocator::getEventDispatcher().clear();
    };
    run("falling sphere, created first", [] { fallingSphereHitsBox(true); });
    run("falling sphere, created second", [] { fallingSphereHitsBox(false); });
    run("dynamic box, static sphere", dynamicBoxHitsStaticSphere);
    run("sphere contacts box edges/corners", sphereContactsBoxEdges);
    run("sphere does not collide as a box", sphereDoesNotCollideAsABox);
    run("sphere inside box", sphereInsideBox);
    run("sphere pairs, including coincident centers", spherePairs);
    run("sphere radius, scale and offset", sphereScaleAndOffset);
    run("box-box regression", boxCollisionStillWorks);
    return failures == 0 ? 0 : 1;
}
