#pragma once

#include "ecs/Entity.h"
#include "math/MathTypes.h"

struct CollisionEvent {
    Entity first = kInvalidEntity;
    Entity second = kInvalidEntity;
    Vec3 normal{};
    float penetration = 0.0f;
};
