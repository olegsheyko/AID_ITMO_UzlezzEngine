#include "resources/SceneManifest.h"

#include "core/Logger.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace {
template <typename TVector>
TVector readVector(const nlohmann::json& node, const TVector& fallback);

ColliderType readColliderType(const nlohmann::json& node, ColliderType fallback) {
    if (!node.is_string()) {
        return fallback;
    }

    const std::string value = node.get<std::string>();
    if (value == "sphere") {
        return ColliderType::Sphere;
    }

    return ColliderType::Box;
}

template <>
Vec3 readVector<Vec3>(const nlohmann::json& node, const Vec3& fallback) {
    if (!node.is_array() || node.size() != 3) {
        return fallback;
    }

    return Vec3{
        node[0].get<float>(),
        node[1].get<float>(),
        node[2].get<float>(),
    };
}

template <>
Vec4 readVector<Vec4>(const nlohmann::json& node, const Vec4& fallback) {
    if (!node.is_array() || node.size() != 4) {
        return fallback;
    }

    return Vec4{
        node[0].get<float>(),
        node[1].get<float>(),
        node[2].get<float>(),
        node[3].get<float>(),
    };
}
}

bool SceneManifest::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open scene manifest: " + path);
        return false;
    }

    nlohmann::json json;
    file >> json;

    meshPaths_.clear();
    texturePaths_.clear();
    shaderPaths_.clear();
    entities_.clear();

    if (json.contains("meshes")) {
        for (auto& [id, meshPath] : json["meshes"].items()) {
            meshPaths_[id] = meshPath.get<std::string>();
        }
    }

    if (json.contains("textures")) {
        for (auto& [id, texturePath] : json["textures"].items()) {
            texturePaths_[id] = texturePath.get<std::string>();
        }
    }

    if (json.contains("shaders")) {
        for (auto& [id, shaderNode] : json["shaders"].items()) {
            shaderPaths_[id] = ShaderAssetPaths{
                shaderNode.value("vertex", ""),
                shaderNode.value("fragment", ""),
            };
        }
    }

    if (json.contains("entities")) {
        for (const auto& entityNode : json["entities"]) {
            SceneEntityDescription entity;
            entity.tag = entityNode.value("tag", "Entity");
            entity.meshId = entityNode.value("mesh", "");
            entity.baseColorTextureId = entityNode.value("texture", "");
            entity.shaderId = entityNode.value("shader", "");
            entity.position = readVector<Vec3>(entityNode.value("position", nlohmann::json::array()), entity.position);
            entity.rotation = readVector<Vec3>(entityNode.value("rotation", nlohmann::json::array()), entity.rotation);
            entity.scale = readVector<Vec3>(entityNode.value("scale", nlohmann::json::array()), entity.scale);
            entity.color = readVector<Vec4>(entityNode.value("color", nlohmann::json::array()), entity.color);
            entity.spinSpeed = entityNode.value("spinSpeed", 0.0f);
            entity.controllable = entityNode.value("controllable", false);

            if (entityNode.contains("rigidbody")) {
                const auto& rigidbodyNode = entityNode["rigidbody"];
                entity.hasRigidbody = true;
                entity.rigidbody.velocity = readVector<Vec3>(rigidbodyNode.value("velocity", nlohmann::json::array()), entity.rigidbody.velocity);
                entity.rigidbody.acceleration = readVector<Vec3>(rigidbodyNode.value("acceleration", nlohmann::json::array()), entity.rigidbody.acceleration);
                entity.rigidbody.mass = rigidbodyNode.value("mass", entity.rigidbody.mass);
                entity.rigidbody.useGravity = rigidbodyNode.value("useGravity", entity.rigidbody.useGravity);
            }

            if (entityNode.contains("collider")) {
                const auto& colliderNode = entityNode["collider"];
                entity.hasCollider = true;
                entity.collider.type = readColliderType(colliderNode.value("type", nlohmann::json()), entity.collider.type);
                entity.collider.halfExtents = readVector<Vec3>(colliderNode.value("halfExtents", nlohmann::json::array()), entity.collider.halfExtents);
                entity.collider.offset = readVector<Vec3>(colliderNode.value("offset", nlohmann::json::array()), entity.collider.offset);
                entity.collider.radius = colliderNode.value("radius", entity.collider.radius);
            }

            entities_.push_back(entity);
        }
    }

    LOG_INFO("Scene manifest loaded: " + path);
    return !entities_.empty();
}

const std::string* SceneManifest::findMeshPath(const std::string& id) const {
    auto it = meshPaths_.find(id);
    return it == meshPaths_.end() ? nullptr : &it->second;
}

const std::string* SceneManifest::findTexturePath(const std::string& id) const {
    auto it = texturePaths_.find(id);
    return it == texturePaths_.end() ? nullptr : &it->second;
}

const ShaderAssetPaths* SceneManifest::findShader(const std::string& id) const {
    auto it = shaderPaths_.find(id);
    return it == shaderPaths_.end() ? nullptr : &it->second;
}
