#include "ecs/SpinSystem.h"
#include "ecs/Components.h"
#include "ecs/World.h"

void SpinSystem::update(World& world, float dt) {
    world.forEach<Spin, Transform>([dt](Entity, Spin& spin, Transform& transform) {
        transform.rotation.y += spin.speed * dt;
    });
}
