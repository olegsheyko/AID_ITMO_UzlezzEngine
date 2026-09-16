#pragma once

#include "math/MathTypes.h"

// Right-handed Z-up world: +X right, -Y front, +Z up.
// A front-view camera sits on -Y and looks into the scene along +Y.
// Pitch rotates about X; yaw rotates about Z. View space stays OpenGL -Z-forward.
namespace CameraMath {
inline Vec3 forward(float pitch, float yaw) {
    const float cosPitch = std::cos(pitch);
    return {-std::sin(yaw) * cosPitch, std::cos(yaw) * cosPitch, std::sin(pitch)};
}

inline Vec3 right(float yaw) {
    return {std::cos(yaw), std::sin(yaw), 0.0f};
}

inline Vec3 up(float pitch, float yaw) {
    const Vec3 f = forward(pitch, yaw);
    const Vec3 r = right(yaw);
    return {r.y * f.z, -r.x * f.z, r.x * f.y - r.y * f.x};
}

inline Mat4 view(const Vec3& position, float pitch, float yaw) {
    const Vec3 f = forward(pitch, yaw);
    const Vec3 r = right(yaw);
    const Vec3 u = up(pitch, yaw);
    Mat4 result = Mat4::identity();
    result.values[0] = r.x;
    result.values[1] = u.x;
    result.values[2] = -f.x;
    result.values[4] = r.y;
    result.values[5] = u.y;
    result.values[6] = -f.y;
    result.values[8] = r.z;
    result.values[9] = u.z;
    result.values[10] = -f.z;
    result.values[12] = -(r.x * position.x + r.y * position.y + r.z * position.z);
    result.values[13] = -(u.x * position.x + u.y * position.y + u.z * position.z);
    result.values[14] = f.x * position.x + f.y * position.y + f.z * position.z;
    return result;
}
} // namespace CameraMath
