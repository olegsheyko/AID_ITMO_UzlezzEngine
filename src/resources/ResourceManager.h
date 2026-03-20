#pragma once

#include "Resource.h"
#include "ResourceTypes.h"
#include <unordered_map>
#include <memory>
#include <string>

class IRenderAdapter;

// Централизованный менеджер ресурсов с кэшированием
class ResourceManager {
public:
    static ResourceManager& getInstance();

    // Инициализация с адаптером рендеринга
    void init(IRenderAdapter* renderer);

    // Загрузка ресурсов с кэшированием
    std::shared_ptr<Resource<MeshData>> loadMesh(const std::string& path);
    std::shared_ptr<Resource<TextureData>> loadTexture(const std::string& path);
    std::shared_ptr<Resource<ShaderData>> loadShader(const std::string& vertexPath, const std::string& fragmentPath);

    // Перезагрузка ресурса (для горячей замены)
    void reloadShader(const std::string& vertexPath, const std::string& fragmentPath);

    // Очистка кэша
    void clearCache();
    
    // Получение статистики
    size_t getMeshCount() const { return meshCache_.size(); }
    size_t getTextureCount() const { return textureCache_.size(); }
    size_t getShaderCount() const { return shaderCache_.size(); }

private:
    ResourceManager() = default;
    ~ResourceManager() = default;
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    IRenderAdapter* renderer_ = nullptr;

    // Кэши для разных типов ресурсов
    std::unordered_map<std::string, std::shared_ptr<Resource<MeshData>>> meshCache_;
    std::unordered_map<std::string, std::shared_ptr<Resource<TextureData>>> textureCache_;
    std::unordered_map<std::string, std::shared_ptr<Resource<ShaderData>>> shaderCache_;
};
