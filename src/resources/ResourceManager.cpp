#include "ResourceManager.h"
#include "MeshLoader.h"
#include "TextureLoader.h"
#include "ShaderLoader.h"
#include "core/Logger.h"
#include "render/IRenderAdapter.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>
#include <vector>

std::string ResourceManager::makeShaderKey(const std::string& vertexPath, const std::string& fragmentPath) {
    return vertexPath + "|" + fragmentPath;
}

ResourceManager& ResourceManager::getInstance() {
    static ResourceManager instance;
    return instance;
}

void ResourceManager::init(IRenderAdapter* renderer) {
    renderer_ = renderer;
    LOG_INFO("ResourceManager initialized");
}

std::shared_ptr<Resource<MeshData>> ResourceManager::loadMesh(const std::string& path) {
    // Проверяем кэш
    auto it = meshCache_.find(path);
    if (it != meshCache_.end()) {
        LOG_INFO("Mesh loaded from cache: " + path);
        return it->second;
    }

    // Загружаем новый меш
    auto resource = std::make_shared<Resource<MeshData>>(path);
    if (MeshLoader::load(path, *resource->getData(), renderer_)) {
        // Resolve material textures once, not during every draw of every submesh.
        for (auto& subMesh : resource->getData()->subMeshes) {
            auto& material = subMesh.material;
            if (!material.diffuseTexturePath.empty()) {
                material.cachedDiffuseTexture = loadTexture(material.diffuseTexturePath);
            }
        }
        resource->setLoaded(true);
        meshCache_[path] = resource;
        LOG_INFO("Mesh loaded: " + path);
        return resource;
    }

    LOG_ERROR("Failed to load mesh: " + path);
    return nullptr;
}

std::shared_ptr<Resource<TextureData>> ResourceManager::loadTexture(const std::string& path) {
    auto it = textureCache_.find(path);
    if (it != textureCache_.end()) {
        LOG_INFO("Texture loaded from cache: " + path);
        return it->second;
    }

    auto resource = std::make_shared<Resource<TextureData>>(path);
    if (TextureLoader::load(path, *resource->getData(), renderer_)) {
        resource->setLoaded(true);
        textureCache_[path] = resource;
        LOG_INFO("Texture loaded: " + path);
        return resource;
    }

    LOG_ERROR("Failed to load texture: " + path);
    return nullptr;
}

std::shared_ptr<Resource<ShaderData>> ResourceManager::loadShader(const std::string& vertexPath, const std::string& fragmentPath) {
    const std::string key = makeShaderKey(vertexPath, fragmentPath);
    
    auto it = shaderCache_.find(key);
    if (it != shaderCache_.end()) {
        LOG_INFO("Shader loaded from cache: " + key);
        return it->second;
    }

    auto resource = std::make_shared<Resource<ShaderData>>(key);
    if (ShaderLoader::load(vertexPath, fragmentPath, *resource->getData(), renderer_)) {
        resource->setLoaded(true);
        shaderCache_[key] = resource;
        LOG_INFO("Shader loaded: " + key);
        return resource;
    }

    LOG_ERROR("Failed to load shader: " + key);
    return nullptr;
}

void ResourceManager::clearCache() {
    if (renderer_ != nullptr) {
        for (auto& [path, resource] : meshCache_) {
            (void)path;
            if (!resource || !resource->isLoaded()) {
                continue;
            }

            MeshData* meshData = resource->getData();
            if (meshData == nullptr) {
                continue;
            }

            for (auto& subMesh : meshData->subMeshes) {
                renderer_->destroyMesh(subMesh.vao, subMesh.vbo, subMesh.ebo);
                subMesh.indexCount = 0;
            }

            if (meshData->subMeshes.empty()) {
                renderer_->destroyMesh(meshData->vao, meshData->vbo, meshData->ebo);
            } else {
                meshData->vao = 0;
                meshData->vbo = 0;
                meshData->ebo = 0;
            }
            meshData->indexCount = 0;
        }

        for (auto& [path, resource] : textureCache_) {
            (void)path;
            if (resource && resource->isLoaded()) {
                renderer_->destroyTexture(resource->getData()->textureId);
            }
        }

        for (auto& [key, resource] : shaderCache_) {
            (void)key;
            if (resource && resource->isLoaded()) {
                renderer_->destroyShaderProgram(resource->getData()->programId);
            }
        }
    }

    meshCache_.clear();
    textureCache_.clear();
    shaderCache_.clear();
    LOG_INFO("Resource cache cleared");
}

void ResourceManager::reloadShader(const std::string& vertexPath, const std::string& fragmentPath) {
    const std::string key = makeShaderKey(vertexPath, fragmentPath);
    
    auto it = shaderCache_.find(key);
    if (it != shaderCache_.end()) {
        auto* shaderData = it->second->getData();
        if (renderer_ != nullptr) {
            renderer_->destroyShaderProgram(shaderData->programId);
        }

        if (ShaderLoader::load(vertexPath, fragmentPath, *shaderData, renderer_)) {
            LOG_INFO("Shader reloaded: " + key);
        } else {
            LOG_ERROR("Failed to reload shader: " + key);
        }
    }
}

void ResourceManager::reloadShadersForFile(const std::string& changedPath) {
    for (auto& [key, resource] : shaderCache_) {
        (void)key;
        if (resource == nullptr || !resource->isLoaded()) {
            continue;
        }

        const ShaderData* shaderData = resource->getData();
        if (shaderData == nullptr) {
            continue;
        }

        if (shaderData->vertexPath == changedPath || shaderData->fragmentPath == changedPath) {
            reloadShader(shaderData->vertexPath, shaderData->fragmentPath);
        }
    }
}

std::vector<std::string> ResourceManager::getMeshIds() const {
    std::vector<std::string> ids;
    ids.reserve(meshCache_.size());
    for (const auto& [id, resource] : meshCache_) {
        (void)resource;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<std::string> ResourceManager::getTextureIds() const {
    std::vector<std::string> ids;
    ids.reserve(textureCache_.size());
    for (const auto& [id, resource] : textureCache_) {
        (void)resource;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<std::string> ResourceManager::getAvailableTexturePaths() const {
    auto paths = getTextureIds();
    std::error_code error;
    std::filesystem::recursive_directory_iterator files(
        ".", std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    for (; !error && files != end; files.increment(error)) {
        if (files->is_directory(error)) {
            const std::string name = files->path().filename().string();
            // Search project content, excluding dependencies, metadata and build copies.
            if ((!name.empty() && name.front() == '.') || name == "external" || name == "build" ||
                name.rfind("build_", 0) == 0 || name == "out" || name == "tests" ||
                name == "docs" || name == "screenshotes") {
                files.disable_recursion_pending();
            }
            continue;
        }
        if (!files->is_regular_file(error)) {
            continue;
        }
        std::string extension = files->path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".dds" || extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
            extension == ".bmp" || extension == ".tga" || extension == ".gif" || extension == ".hdr" ||
            extension == ".psd" || extension == ".pic") {
            paths.push_back(files->path().lexically_normal().generic_string());
        }
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

std::vector<std::string> ResourceManager::getShaderIds() const {
    std::vector<std::string> ids;
    ids.reserve(shaderCache_.size());
    for (const auto& [id, resource] : shaderCache_) {
        (void)resource;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

size_t ResourceManager::estimateMemoryUsageBytes() const {
    size_t total = 0;

    for (const auto& [path, resource] : meshCache_) {
        (void)path;
        if (!resource || !resource->isLoaded() || resource->getData() == nullptr) {
            continue;
        }

        const MeshData& mesh = *resource->getData();
        total += mesh.vertices.size() * sizeof(Vertex);
        total += mesh.indices.size() * sizeof(uint32_t);
        for (const SubMesh& subMesh : mesh.subMeshes) {
            total += subMesh.vertices.size() * sizeof(Vertex);
            total += subMesh.indices.size() * sizeof(uint32_t);
        }
    }

    for (const auto& [path, resource] : textureCache_) {
        (void)path;
        if (!resource || !resource->isLoaded() || resource->getData() == nullptr) {
            continue;
        }

        const TextureData& texture = *resource->getData();
        if (texture.width > 0 && texture.height > 0 && texture.channels > 0) {
            total += static_cast<size_t>(texture.width) * static_cast<size_t>(texture.height) * static_cast<size_t>(texture.channels);
        }
    }

    for (const auto& [path, resource] : shaderCache_) {
        (void)path;
        if (!resource || !resource->isLoaded() || resource->getData() == nullptr) {
            continue;
        }

        const ShaderData& shader = *resource->getData();
        total += shader.vertexSource.size();
        total += shader.fragmentSource.size();
    }

    return total;
}
