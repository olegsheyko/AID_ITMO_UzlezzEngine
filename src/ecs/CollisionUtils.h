#pragma once

#include "ecs/Components.h"

#include <algorithm>
#include <cmath>

struct AABB {
    Vec3 center{};
    Vec3 halfSize{};
};

struct Sphere {
    Vec3 center{};
    float radius = 0.0f;
};

namespace CollisionUtils {
inline float absolute(float value) {
    return std::fabs(value);
}

inline Vec3 colliderCenter(const Transform& transform, const Collider& collider) {
    return Vec3{
        transform.position.x + collider.offset.x,
        transform.position.y + collider.offset.y,
        transform.position.z + collider.offset.z
    };
}

inline Sphere buildSphere(const Transform& transform, const Collider& collider) {
    // Keep a sphere under non-uniform (including mirrored) scale.
    const float maxScale = std::max({absolute(transform.scale.x), absolute(transform.scale.y), absolute(transform.scale.z)});
    return Sphere{colliderCenter(transform, collider), absolute(collider.radius) * maxScale};
}

inline Vec3 scaledHalfExtents(const Transform& transform, const Collider& collider) {
    return Vec3{
        absolute(collider.halfExtents.x * transform.scale.x),
        absolute(collider.halfExtents.y * transform.scale.y),
        absolute(collider.halfExtents.z * transform.scale.z)
    };
}

inline AABB buildAABB(const Transform& transform, const Collider& collider) {
    return AABB{
        colliderCenter(transform, collider),
        scaledHalfExtents(transform, collider)
    };
}
}
