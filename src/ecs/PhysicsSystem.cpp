#include "core/ServiceLocator.h"
#include "ecs/PhysicsSystem.h"

#include "ecs/CollisionUtils.h"
#include "ecs/Components.h"
#include "ecs/Entity.h"
#include "ecs/World.h"
#include "events/CollisionEvent.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
struct CollisionManifold {
    // Points from the right collider toward the left collider.
    Vec3 normal{};
    float penetration = 0.0f;
};

Vec3 add(const Vec3& left, const Vec3& right) {
    return Vec3{left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 subtract(const Vec3& left, const Vec3& right) {
    return Vec3{left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 scale(const Vec3& value, float scalar) {
    return Vec3{value.x * scalar, value.y * scalar, value.z * scalar};
}

float dot(const Vec3& left, const Vec3& right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

bool intersects(const AABB& left, const AABB& right, CollisionManifold& outManifold) {
    const float dx = CollisionUtils::absolute(left.center.x - right.center.x);
    const float dy = CollisionUtils::absolute(left.center.y - right.center.y);
    const float dz = CollisionUtils::absolute(left.center.z - right.center.z);

    const float overlapX = (left.halfSize.x + right.halfSize.x) - dx;
    if (overlapX <= 0.0f) {
        return false;
    }

    const float overlapY = (left.halfSize.y + right.halfSize.y) - dy;
    if (overlapY <= 0.0f) {
        return false;
    }

    const float overlapZ = (left.halfSize.z + right.halfSize.z) - dz;
    if (overlapZ <= 0.0f) {
        return false;
    }

    const Vec3 delta = subtract(right.center, left.center);

    outManifold.penetration = overlapX;
    outManifold.normal = Vec3{delta.x >= 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f};

    if (overlapY < outManifold.penetration) {
        outManifold.penetration = overlapY;
        outManifold.normal = Vec3{0.0f, delta.y >= 0.0f ? -1.0f : 1.0f, 0.0f};
    }

    if (overlapZ < outManifold.penetration) {
        outManifold.penetration = overlapZ;
        outManifold.normal = Vec3{0.0f, 0.0f, delta.z >= 0.0f ? -1.0f : 1.0f};
    }

    return true;
}

bool intersects(const Sphere& left, const Sphere& right, CollisionManifold& outManifold) {
    if (left.radius <= 0.0f || right.radius <= 0.0f) {
        return false;
    }

    const Vec3 delta = subtract(left.center, right.center);
    const float distanceSquared = dot(delta, delta);
    const float radii = left.radius + right.radius;
    if (distanceSquared >= radii * radii) {
        return false;
    }

    const float distance = std::sqrt(distanceSquared);
    outManifold.normal = distance > 0.0f ? scale(delta, 1.0f / distance) : Vec3{1.0f, 0.0f, 0.0f};
    outManifold.penetration = radii - distance;
    return true;
}

bool intersects(const Sphere& sphere, const AABB& box, CollisionManifold& outManifold) {
    if (sphere.radius <= 0.0f) {
        return false;
    }

    const Vec3 closest{
        std::clamp(sphere.center.x, box.center.x - box.halfSize.x, box.center.x + box.halfSize.x),
        std::clamp(sphere.center.y, box.center.y - box.halfSize.y, box.center.y + box.halfSize.y),
        std::clamp(sphere.center.z, box.center.z - box.halfSize.z, box.center.z + box.halfSize.z)
    };
    const Vec3 delta = subtract(sphere.center, closest);
    const float distanceSquared = dot(delta, delta);
    if (distanceSquared >= sphere.radius * sphere.radius) {
        return false;
    }

    if (distanceSquared > 0.0f) {
        const float distance = std::sqrt(distanceSquared);
        outManifold.normal = scale(delta, 1.0f / distance);
        outManifold.penetration = sphere.radius - distance;
        return true;
    }

    // The center is inside/on the box: exit through the nearest face.
    const Vec3 relative = subtract(sphere.center, box.center);
    float faceDistance = box.halfSize.x - std::fabs(relative.x);
    outManifold.normal = Vec3{relative.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f};
    const float yDistance = box.halfSize.y - std::fabs(relative.y);
    if (yDistance < faceDistance) {
        faceDistance = yDistance;
        outManifold.normal = Vec3{0.0f, relative.y >= 0.0f ? 1.0f : -1.0f, 0.0f};
    }
    const float zDistance = box.halfSize.z - std::fabs(relative.z);
    if (zDistance < faceDistance) {
        faceDistance = zDistance;
        outManifold.normal = Vec3{0.0f, 0.0f, relative.z >= 0.0f ? 1.0f : -1.0f};
    }
    outManifold.penetration = sphere.radius + faceDistance;
    return true;
}

bool intersects(
    const Transform& leftTransform, const Collider& left,
    const Transform& rightTransform, const Collider& right,
    CollisionManifold& outManifold) {
    if (left.type == ColliderType::Box && right.type == ColliderType::Box) {
        return intersects(CollisionUtils::buildAABB(leftTransform, left), CollisionUtils::buildAABB(rightTransform, right), outManifold);
    }
    if (left.type == ColliderType::Sphere && right.type == ColliderType::Sphere) {
        return intersects(CollisionUtils::buildSphere(leftTransform, left), CollisionUtils::buildSphere(rightTransform, right), outManifold);
    }
    if (left.type == ColliderType::Sphere && right.type == ColliderType::Box) {
        return intersects(CollisionUtils::buildSphere(leftTransform, left), CollisionUtils::buildAABB(rightTransform, right), outManifold);
    }
    if (left.type == ColliderType::Box && right.type == ColliderType::Sphere) {
        if (intersects(CollisionUtils::buildSphere(rightTransform, right), CollisionUtils::buildAABB(leftTransform, left), outManifold)) {
            outManifold.normal = scale(outManifold.normal, -1.0f);
            return true;
        }
    }
    return false;
}

bool isDynamic(const Rigidbody* rigidbody) {
    return rigidbody != nullptr && rigidbody->mass > 0.0f;
}

Vec3 reflectVelocity(const Vec3& velocity, const Vec3& normal, float restitution) {
    const float velocityAlongNormal = dot(velocity, normal);
    if (velocityAlongNormal >= 0.0f) {
        return velocity;
    }

    return subtract(velocity, scale(normal, (1.0f + restitution) * velocityAlongNormal));
}

void resolveCollision(
    Transform& leftTransform,
    Rigidbody* leftBody,
    Transform& rightTransform,
    Rigidbody* rightBody,
    const CollisionManifold& manifold) {
    const bool leftDynamic = isDynamic(leftBody);
    const bool rightDynamic = isDynamic(rightBody);
    if (!leftDynamic && !rightDynamic) {
        return;
    }

    const Vec3 separation = scale(manifold.normal, manifold.penetration);
    if (leftDynamic && rightDynamic) {
        leftTransform.position = add(leftTransform.position, scale(separation, 0.5f));
        rightTransform.position = subtract(rightTransform.position, scale(separation, 0.5f));
    } else if (leftDynamic) {
        leftTransform.position = add(leftTransform.position, separation);
    } else if (rightDynamic) {
        rightTransform.position = subtract(rightTransform.position, separation);
    }

    constexpr float kRestitution = 1.0f;
    if (leftDynamic && leftBody != nullptr) {
        leftBody->velocity = reflectVelocity(leftBody->velocity, manifold.normal, kRestitution);
    }
    if (rightDynamic && rightBody != nullptr) {
        rightBody->velocity = reflectVelocity(rightBody->velocity, scale(manifold.normal, -1.0f), kRestitution);
    }
}
}

PhysicsSystem::PhysicsSystem(float gravityStrength)
    : gravityStrength_(gravityStrength) {
}

void PhysicsSystem::update(World& world, float dt) {
    lastCollisionCount_ = 0;
    if (dt <= 0.0f) {
        return;
    }

    world.forEach<Transform, Rigidbody>([this, dt](Entity, Transform& transform, Rigidbody& rigidbody) {
        if (rigidbody.mass <= 0.0f) {
            return;
        }

        if (rigidbody.useGravity) {
            rigidbody.velocity.y -= gravityStrength_ * dt;
        }

        rigidbody.velocity = add(rigidbody.velocity, scale(rigidbody.acceleration, dt));
        transform.position = add(transform.position, scale(rigidbody.velocity, dt));
    });

    std::vector<Entity> colliders;
    world.forEach<Transform, Collider>([&world, &colliders](Entity entity, Transform&, Collider&) {
        if (!world.isAlive(entity)) {
            return;
        }

        colliders.push_back(entity);
    });

    for (std::size_t leftIndex = 0; leftIndex < colliders.size(); ++leftIndex) {
        for (std::size_t rightIndex = leftIndex + 1; rightIndex < colliders.size(); ++rightIndex) {
            const Entity leftEntity = colliders[leftIndex];
            const Entity rightEntity = colliders[rightIndex];

            if (!world.isAlive(leftEntity) || !world.isAlive(rightEntity)) {
                continue;
            }

            Transform& leftTransform = world.getComponent<Transform>(leftEntity);
            Transform& rightTransform = world.getComponent<Transform>(rightEntity);
            const Collider& leftCollider = world.getComponent<Collider>(leftEntity);
            const Collider& rightCollider = world.getComponent<Collider>(rightEntity);

            CollisionManifold manifold;
            if (!intersects(leftTransform, leftCollider, rightTransform, rightCollider, manifold)) {
                continue;
            }

            ++lastCollisionCount_;

            Rigidbody* leftBody = world.hasComponent<Rigidbody>(leftEntity) ? &world.getComponent<Rigidbody>(leftEntity) : nullptr;
            Rigidbody* rightBody = world.hasComponent<Rigidbody>(rightEntity) ? &world.getComponent<Rigidbody>(rightEntity) : nullptr;
            resolveCollision(leftTransform, leftBody, rightTransform, rightBody, manifold);
            ServiceLocator::getEventDispatcher().dispatch(CollisionEvent{
                leftEntity,
                rightEntity,
                manifold.normal,
                manifold.penetration
            });
        }
    }
}
