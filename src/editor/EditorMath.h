#pragma once

#include "ecs/CollisionUtils.h"
#include "math/MathTypes.h"

#include <algorithm>
#include <cmath>
#include <limits>

// Векторные мелочи и разбор матриц для редактора. Порядок вращений совпадает с Math::composeTransform:
// R = Ry * Rx * Rz (векторы-столбцы), матрицы хранятся по столбцам.
namespace EditorMath {
constexpr float kPi = 3.1415926f;

inline Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 mul(const Vec3& v, float s) { return {v.x * s, v.y * s, v.z * s}; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) {
    const float len = length(v);
    return len > 1e-6f ? mul(v, 1.0f / len) : Vec3{};
}

inline float toDegrees(float radians) { return radians * 180.0f / kPi; }
inline float toRadians(float degrees) { return degrees * kPi / 180.0f; }

inline Vec3 transformPoint(const Mat4& m, const Vec3& p) {
    const auto& v = m.values;
    return {
        v[0] * p.x + v[4] * p.y + v[8] * p.z + v[12],
        v[1] * p.x + v[5] * p.y + v[9] * p.z + v[13],
        v[2] * p.x + v[6] * p.y + v[10] * p.z + v[14]};
}

// Проекция точки мира в экран вьюпорта; false — точка позади камеры.
inline bool projectToScreen(const Mat4& viewProjection, const Vec3& point, float viewportX, float viewportY,
    float viewportWidth, float viewportHeight, float& outX, float& outY) {
    const auto& v = viewProjection.values;
    const float x = v[0] * point.x + v[4] * point.y + v[8] * point.z + v[12];
    const float y = v[1] * point.x + v[5] * point.y + v[9] * point.z + v[13];
    const float w = v[3] * point.x + v[7] * point.y + v[11] * point.z + v[15];
    if (w <= 0.0001f) {
        return false;
    }
    outX = viewportX + (x / w * 0.5f + 0.5f) * viewportWidth;
    outY = viewportY + (1.0f - (y / w * 0.5f + 0.5f)) * viewportHeight;
    return true;
}

inline Mat4 inverse(const Mat4& matrix) {
    return Math::inverse(matrix);
}

// Обратная операция к Math::composeTransform.
inline void decompose(const Mat4& matrix, Vec3& position, Vec3& rotation, Vec3& scale) {
    const auto& m = matrix.values;
    position = {m[12], m[13], m[14]};
    const Vec3 column0{m[0], m[1], m[2]};
    const Vec3 column1{m[4], m[5], m[6]};
    const Vec3 column2{m[8], m[9], m[10]};
    scale = {length(column0), length(column1), length(column2)};
    const float sx = scale.x > 1e-6f ? scale.x : 1.0f;
    const float sy = scale.y > 1e-6f ? scale.y : 1.0f;
    const float sz = scale.z > 1e-6f ? scale.z : 1.0f;
    // r[row][col] чистого вращения.
    const float r00 = m[0] / sx, r10 = m[1] / sx, r20 = m[2] / sx;
    const float r01 = m[4] / sy, r11 = m[5] / sy, r21 = m[6] / sy;
    const float r02 = m[8] / sz, r12 = m[9] / sz, r22 = m[10] / sz;
    const float sinX = std::clamp(-r12, -1.0f, 1.0f);
    rotation.x = std::asin(sinX);
    if (std::abs(sinX) < 0.9999f) {
        rotation.y = std::atan2(r02, r22);
        rotation.z = std::atan2(r10, r11);
    } else {
        // Шарнирный замок: Y и Z вращают вокруг одной оси, всё отдаём Y.
        rotation.z = 0.0f;
        rotation.y = std::atan2(-r20, r00);
        (void)r01;
        (void)r21;
    }
}

inline float nearestEquivalentAngle(float candidate, float reference) {
    constexpr float twoPi = kPi * 2.0f;
    while (candidate - reference > kPi) {
        candidate -= twoPi;
    }
    while (candidate - reference < -kPi) {
        candidate += twoPi;
    }
    return candidate;
}

inline Vec3 nearestEquivalentEuler(const Vec3& candidate, const Vec3& reference) {
    return {
        nearestEquivalentAngle(candidate.x, reference.x),
        nearestEquivalentAngle(candidate.y, reference.y),
        nearestEquivalentAngle(candidate.z, reference.z)};
}

// AABB в мире для локального бокса min..max под матрицей.
inline AABB transformBounds(const Mat4& matrix, const Vec3& localMin, const Vec3& localMax) {
    Vec3 worldMin{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 worldMax{-worldMin.x, -worldMin.y, -worldMin.z};
    for (int corner = 0; corner < 8; ++corner) {
        const Vec3 local{
            (corner & 1) ? localMax.x : localMin.x,
            (corner & 2) ? localMax.y : localMin.y,
            (corner & 4) ? localMax.z : localMin.z};
        const Vec3 p = transformPoint(matrix, local);
        worldMin = {std::min(worldMin.x, p.x), std::min(worldMin.y, p.y), std::min(worldMin.z, p.z)};
        worldMax = {std::max(worldMax.x, p.x), std::max(worldMax.y, p.y), std::max(worldMax.z, p.z)};
    }
    return AABB{mul(add(worldMin, worldMax), 0.5f), mul(sub(worldMax, worldMin), 0.5f)};
}

inline bool rayIntersectsAABB(const Vec3& origin, const Vec3& direction, const AABB& aabb, float& outDistance) {
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();
    auto testAxis = [&](float originValue, float directionValue, float minValue, float maxValue) {
        if (std::abs(directionValue) < 0.00001f) {
            return originValue >= minValue && originValue <= maxValue;
        }
        float first = (minValue - originValue) / directionValue;
        float second = (maxValue - originValue) / directionValue;
        if (first > second) {
            std::swap(first, second);
        }
        tMin = std::max(tMin, first);
        tMax = std::min(tMax, second);
        return tMin <= tMax;
    };
    if (!testAxis(origin.x, direction.x, aabb.center.x - aabb.halfSize.x, aabb.center.x + aabb.halfSize.x) ||
        !testAxis(origin.y, direction.y, aabb.center.y - aabb.halfSize.y, aabb.center.y + aabb.halfSize.y) ||
        !testAxis(origin.z, direction.z, aabb.center.z - aabb.halfSize.z, aabb.center.z + aabb.halfSize.z)) {
        return false;
    }
    outDistance = tMin;
    return tMax >= 0.0f;
}
} // namespace EditorMath
