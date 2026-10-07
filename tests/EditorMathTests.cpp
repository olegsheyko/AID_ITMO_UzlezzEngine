#include "editor/EditorMath.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace EditorMath;

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool sameMatrix(const Mat4& a, const Mat4& b, float epsilon = 1e-4f) {
    for (int i = 0; i < 16; ++i) {
        if (std::abs(a.values[i] - b.values[i]) > epsilon) {
            return false;
        }
    }
    return true;
}

Mat4 compose(const Vec3& rotation) {
    return Math::composeTransform(Vec3{1.0f, -2.0f, 3.0f}, rotation, Vec3{1.5f, 0.5f, 2.0f});
}

// Поворот вокруг мировой оси через точку объекта — так его выдаёт гизмо ImGuizmo.
Mat4 rotateAboutWorldAxis(const Mat4& model, int axis, float radians) {
    const Mat4 rotation = axis == 0 ? Math::rotationX(radians) : axis == 1 ? Math::rotationY(radians) : Math::rotationZ(radians);
    const Vec3 pivot{model.values[12], model.values[13], model.values[14]};
    return Math::multiply(Math::translation(pivot),
        Math::multiply(rotation, Math::multiply(Math::translation(Vec3{-pivot.x, -pivot.y, -pivot.z}), model)));
}

void decomposeRestoresComposedMatrix() {
    const float angles[] = {-2.9f, -1.6f, -0.7f, 0.0f, 0.3f, 1.2f, 1.5707963f, 2.4f};
    for (float x : angles) {
        for (float y : angles) {
            for (float z : angles) {
                const Mat4 model = compose(Vec3{x, y, z});
                Vec3 position{};
                Vec3 rotation{};
                Vec3 scale{};
                decompose(model, position, rotation, scale);
                const Mat4 restored = Math::composeTransform(position, rotation, scale);
                require(sameMatrix(model, restored), "decompose must invert composeTransform for (" +
                    std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")");
            }
        }
    }
}

// Тот самый баг: у модели X = 90°, гизмо крутит её вокруг мировой вертикали.
// Старый код прибавлял углы поворота к эйлеровым и получал вращение вокруг другой оси.
void gizmoRotationAboutWorldAxisIsPreserved() {
    const Vec3 starts[] = {{1.5707963f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.4f, -0.9f, 1.1f}, {1.5707963f, 0.7f, 0.0f}};
    for (const Vec3& start : starts) {
        for (int axis = 0; axis < 3; ++axis) {
            Mat4 model = compose(start);
            Vec3 rotation = start;
            // Несколько шагов подряд, как при перетаскивании кольца.
            for (int step = 0; step < 12; ++step) {
                model = rotateAboutWorldAxis(model, axis, 0.15f);
                Vec3 position{};
                Vec3 scale{};
                decomposeNear(model, rotation, position, rotation, scale);
                const Mat4 restored = Math::composeTransform(position, rotation, scale);
                require(sameMatrix(model, restored), "gizmo rotation about world axis " + std::to_string(axis) +
                    " must be reproduced by the stored Euler angles (step " + std::to_string(step) + ")");
            }
        }
    }
}

void decomposeNearKeepsAnglesContinuous() {
    // Вращаем вокруг Y через 90°: углы должны идти 80°, 100°, 120°, а не прыгать на 60° с X=Z=180°.
    Vec3 rotation{};
    for (float degrees = 0.0f; degrees <= 170.0f; degrees += 10.0f) {
        const Mat4 model = compose(Vec3{0.0f, toRadians(degrees), 0.0f});
        Vec3 position{};
        Vec3 scale{};
        decomposeNear(model, rotation, position, rotation, scale);
        require(std::abs(toDegrees(rotation.y) - degrees) < 0.05f && std::abs(rotation.x) < 1e-3f && std::abs(rotation.z) < 1e-3f,
            "rotation about Y must stay continuous at " + std::to_string(degrees) + " degrees, got y=" +
            std::to_string(toDegrees(rotation.y)) + " x=" + std::to_string(toDegrees(rotation.x)));
    }
}

// Персонаж Y-up с X = 90° (как префаб врага) при повороте гизмо вокруг мировой Z меняет только рысканье.
void yawOfTiltedCharacterStaysInZ() {
    Vec3 rotation{1.5707963f, 0.0f, 0.0f};
    Mat4 model = compose(rotation);
    for (int step = 1; step <= 30; ++step) {
        model = rotateAboutWorldAxis(model, 2, 0.15f);
        Vec3 position{};
        Vec3 scale{};
        decomposeNear(model, rotation, position, rotation, scale);
        require(std::abs(rotation.x - 1.5707963f) < 1e-3f && std::abs(rotation.y) < 1e-3f &&
            std::abs(nearestEquivalentAngle(rotation.z, 0.15f * step) - 0.15f * step) < 1e-3f,
            "yaw of a Y-up character must stay in Z, step " + std::to_string(step) + ": x=" + std::to_string(toDegrees(rotation.x)) +
            " y=" + std::to_string(toDegrees(rotation.y)) + " z=" + std::to_string(toDegrees(rotation.z)));
    }
}
}

int main() {
    try {
        decomposeRestoresComposedMatrix();
        gizmoRotationAboutWorldAxisIsPreserved();
        decomposeNearKeepsAnglesContinuous();
        yawOfTiltedCharacterStaysInZ();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "EditorMathTests passed\n";
    return 0;
}
