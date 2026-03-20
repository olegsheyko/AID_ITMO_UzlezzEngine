#pragma once

#include <array>
#include <cmath>

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vec4 {
    float x = 1.0f;
    float y = 1.0f;
    float z = 1.0f;
    float w = 1.0f;
};

struct Mat4 {
    std::array<float, 16> values{};

    const float* data() const { return values.data(); }
    float* data() { return values.data(); }

    static Mat4 identity() {
        Mat4 matrix;
        matrix.values = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        return matrix;
    }
};

namespace Math {
inline Mat4 multiply(const Mat4& left, const Mat4& right) {
    Mat4 result{};

    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float value = 0.0f;
            for (int i = 0; i < 4; ++i) {
                value += left.values[i * 4 + row] * right.values[column * 4 + i];
            }
            result.values[column * 4 + row] = value;
        }
    }

    return result;
}

inline Mat4 translation(const Vec3& position) {
    Mat4 matrix = Mat4::identity();
    matrix.values[12] = position.x;
    matrix.values[13] = position.y;
    matrix.values[14] = position.z;
    return matrix;
}

inline Mat4 rotationZ(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);

    Mat4 matrix = Mat4::identity();
    matrix.values[0] = c;
    matrix.values[1] = s;
    matrix.values[4] = -s;
    matrix.values[5] = c;
    return matrix;
}

inline Mat4 rotationY(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);

    Mat4 matrix = Mat4::identity();
    matrix.values[0] = c;
    matrix.values[2] = -s;
    matrix.values[8] = s;
    matrix.values[10] = c;
    return matrix;
}

inline Mat4 rotationX(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);

    Mat4 matrix = Mat4::identity();
    matrix.values[5] = c;
    matrix.values[6] = s;
    matrix.values[9] = -s;
    matrix.values[10] = c;
    return matrix;
}

inline Mat4 scale(const Vec3& scale) {
    Mat4 matrix = Mat4::identity();
    matrix.values[0] = scale.x;
    matrix.values[5] = scale.y;
    matrix.values[10] = scale.z;
    return matrix;
}

inline Mat4 composeTransform(const Vec3& position, const Vec3& rotation, const Vec3& scaleValue) {
    // Применяем вращения в порядке: Y -> X -> Z (стандартный порядок Euler)
    Mat4 rotMatrix = multiply(rotationY(rotation.y), multiply(rotationX(rotation.x), rotationZ(rotation.z)));
    return multiply(translation(position), multiply(rotMatrix, scale(scaleValue)));
}

inline Mat4 perspective(float fovRadians, float aspect, float near, float far) {
    Mat4 matrix{};
    const float tanHalfFov = std::tan(fovRadians / 2.0f);
    
    matrix.values[0] = 1.0f / (aspect * tanHalfFov);
    matrix.values[5] = 1.0f / tanHalfFov;
    matrix.values[10] = -(far + near) / (far - near);
    matrix.values[11] = -1.0f;
    matrix.values[14] = -(2.0f * far * near) / (far - near);
    
    return matrix;
}
} // namespace Math
