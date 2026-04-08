#include "ecs/DebugRenderSystem.h"

#include "ecs/Components.h"
#include "ecs/World.h"
#include "render/IRenderAdapter.h"

#include <cmath>

namespace {
Vec3 absoluteScale(const Vec3& scale) {
    return Vec3{std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)};
}
}

DebugRenderSystem::DebugRenderSystem(IRenderAdapter& renderer)
    : renderer_(renderer) {
}

void DebugRenderSystem::render(World& world) {
    if (!enabled_) {
        return;
    }

    world.forEach<Transform, Collider>([this](Entity, Transform& transform, Collider& collider) {
        if (collider.type != ColliderType::Box) {
            return;
        }

        const Vec3 entityScale = absoluteScale(transform.scale);
        const Vec3 halfExtents{
            collider.halfExtents.x * entityScale.x,
            collider.halfExtents.y * entityScale.y,
            collider.halfExtents.z * entityScale.z
        };
        const Vec3 center{
            transform.position.x + collider.offset.x,
            transform.position.y + collider.offset.y,
            transform.position.z + collider.offset.z
        };

        renderer_.drawDebugAABB(center, halfExtents, Vec4{1.0f, 0.0f, 0.0f, 0.5f});
    });
}
