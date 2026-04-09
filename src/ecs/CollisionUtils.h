#pragma once

#include "ecs/Components.h"

#include <cmath>

struct AABB {
    Vec3 center{};
    Vec3 halfSize{};
};

namespace CollisionUtils {
inline float absolute(float value) {
    return std::fabs(value);
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
        Vec3{
            transform.position.x + collider.offset.x,
            transform.position.y + collider.offset.y,
            transform.position.z + collider.offset.z
        },
        scaledHalfExtents(transform, collider)
    };
}
}
