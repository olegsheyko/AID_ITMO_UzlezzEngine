#pragma once

#include "ecs/Component.h"
#include "ecs/Entity.h"
#include "math/MathTypes.h"
#include "resources/Resource.h"
#include "resources/ResourceTypes.h"

#include <memory>
#include <string>
#include <vector>

struct Transform {
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct Tag {
    std::string name;
};

struct MeshRenderer {
    std::string meshId;
    std::string baseColorTextureId;
    std::string normalTextureId;
    std::string metallicTextureId;
    std::string roughnessTextureId;
    std::string aoTextureId;
    std::string heightTextureId;
    std::string shaderId;

    std::shared_ptr<Resource<MeshData>> cachedMesh;
    std::shared_ptr<Resource<TextureData>> cachedBaseColorTexture;
    std::shared_ptr<Resource<TextureData>> cachedNormalTexture;
    std::shared_ptr<Resource<TextureData>> cachedMetallicTexture;
    std::shared_ptr<Resource<TextureData>> cachedRoughnessTexture;
    std::shared_ptr<Resource<TextureData>> cachedAOTexture;
    std::shared_ptr<Resource<TextureData>> cachedHeightTexture;
    std::shared_ptr<Resource<ShaderData>> cachedShader;
};

struct Hierarchy {
    Entity parent = kInvalidEntity;
    std::vector<Entity> children;
};

struct Spin {
    float speed = 0.0f;
};

struct Camera {
    float fovDegrees = 45.0f;
    float nearClip = 0.1f;
    float farClip = 100.0f;
    float aspectRatio = 800.0f / 600.0f;
    bool active = true;
    Mat4 viewMatrix = Mat4::identity();
    Mat4 projectionMatrix = Mat4::identity();
};

enum class ColliderType {
    Box,
    Sphere
};

struct Rigidbody {
    Vec3 velocity{};
    Vec3 acceleration{};
    float mass = 1.0f;
    bool useGravity = true;
};

struct Collider {
    ColliderType type = ColliderType::Box;
    Vec3 halfExtents{0.5f, 0.5f, 0.5f};
    Vec3 offset{};
    float radius = 0.5f;
};

template <>
struct IsComponent<Transform> : std::true_type {
};

template <>
struct IsComponent<Tag> : std::true_type {
};

template <>
struct IsComponent<MeshRenderer> : std::true_type {
};

template <>
struct IsComponent<Hierarchy> : std::true_type {
};

template <>
struct IsComponent<Spin> : std::true_type {
};

template <>
struct IsComponent<Camera> : std::true_type {
};

template <>
struct IsComponent<Rigidbody> : std::true_type {
};

template <>
struct IsComponent<Collider> : std::true_type {
};
