#include "ecs/DebugRenderSystem.h"

#include "ecs/CollisionUtils.h"
#include "ecs/Components.h"
#include "ecs/World.h"
#include "math/MathTypes.h"
#include "render/IRenderAdapter.h"

namespace {
void resolveCameraMatrices(World& world, IRenderAdapter& renderer, Mat4& viewMatrix, Mat4& projectionMatrix) {
    viewMatrix = Mat4::identity();
    projectionMatrix = Mat4::identity();

    bool cameraFound = false;
    world.forEach<Transform, Camera>([&](Entity, Transform&, Camera& camera) {
        if (cameraFound || !camera.active) {
            return;
        }

        viewMatrix = camera.viewMatrix;
        projectionMatrix = camera.projectionMatrix;
        cameraFound = true;
    });

    if (!cameraFound) {
        viewMatrix.data()[14] = -5.0f;
        int width = 0;
        int height = 0;
        renderer.getFramebufferSize(width, height);
        const float aspect = (width > 0 && height > 0) ? static_cast<float>(width) / static_cast<float>(height) : (800.0f / 600.0f);
        projectionMatrix = Math::perspective(45.0f * 3.1415926f / 180.0f, aspect, 0.1f, 100.0f);
    }
}
}

DebugRenderSystem::DebugRenderSystem(IRenderAdapter& renderer)
    : renderer_(renderer) {
}

void DebugRenderSystem::render(World& world) {
    if (!enabled_) {
        return;
    }

    Mat4 viewMatrix = Mat4::identity();
    Mat4 projectionMatrix = Mat4::identity();
    resolveCameraMatrices(world, renderer_, viewMatrix, projectionMatrix);

    world.forEach<Transform, Collider>([this, &viewMatrix, &projectionMatrix](Entity, Transform& transform, Collider& collider) {
        if (collider.type != ColliderType::Box) {
            return;
        }

        const AABB aabb = CollisionUtils::buildAABB(transform, collider);

        renderer_.drawDebugAABB(aabb.center, aabb.halfSize, Vec4{1.0f, 0.0f, 0.0f, 0.5f}, viewMatrix, projectionMatrix);
    });
}
