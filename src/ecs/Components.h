#pragma once

#include "ecs/Component.h"
#include "math/MathTypes.h"
#include "ecs/Entity.h"
#include "render/RenderTypes.h"
#include "resources/Resource.h"
#include "resources/ResourceTypes.h"

#include <string>
#include <vector>
#include <memory>

struct Transform {
    Vec3 position{};
    Vec3 rotation{}; // Euler angles (x, y, z) в радианах
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct Tag {
    std::string name;
};

struct MeshRenderer {
    // Пути к ресурсам
    std::string meshPath;
    std::string baseColorTexturePath;
    std::string normalTexturePath;
    std::string metallicTexturePath;
    std::string roughnessTexturePath;
    std::string aoTexturePath;
    std::string heightTexturePath;
    std::string vertexShaderPath;
    std::string fragmentShaderPath;
    
    // Цвет (для тинта или если нет текстуры)
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    
    // Для обратной совместимости
    PrimitiveType primitive = PrimitiveType::Triangle;
    
    // Кэшированные ресурсы (загружаются один раз)
    std::shared_ptr<Resource<MeshData>> cachedMesh;
    std::shared_ptr<Resource<TextureData>> cachedBaseColorTexture;
    std::shared_ptr<Resource<TextureData>> cachedNormalTexture;
    std::shared_ptr<Resource<TextureData>> cachedMetallicTexture;
    std::shared_ptr<Resource<TextureData>> cachedRoughnessTexture;
    std::shared_ptr<Resource<TextureData>> cachedAOTexture;
    std::shared_ptr<Resource<TextureData>> cachedHeightTexture;
    std::shared_ptr<Resource<ShaderData>> cachedShader;
    bool resourcesLoaded = false;
};

struct Hierarchy {
    Entity parent = kInvalidEntity;
    std::vector<Entity> children;
};

struct Spin {
    float speed = 0.0f;
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
