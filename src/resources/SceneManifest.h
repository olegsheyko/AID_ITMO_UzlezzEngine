#pragma once

#include "math/MathTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

struct ShaderAssetPaths {
    std::string vertexPath;
    std::string fragmentPath;
};

struct SceneEntityDescription {
    std::string tag;
    std::string meshId;
    std::string baseColorTextureId;
    std::string shaderId;
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float spinSpeed = 0.0f;
    bool controllable = false;
};

class SceneManifest {
public:
    bool loadFromFile(const std::string& path);

    const std::string* findMeshPath(const std::string& id) const;
    const std::string* findTexturePath(const std::string& id) const;
    const ShaderAssetPaths* findShader(const std::string& id) const;
    const std::vector<SceneEntityDescription>& getEntities() const { return entities_; }

private:
    std::unordered_map<std::string, std::string> meshPaths_;
    std::unordered_map<std::string, std::string> texturePaths_;
    std::unordered_map<std::string, ShaderAssetPaths> shaderPaths_;
    std::vector<SceneEntityDescription> entities_;
};
