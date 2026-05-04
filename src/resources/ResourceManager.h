#pragma once

#include "Resource.h"
#include "ResourceTypes.h"

#include <string>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <vector>

class IRenderAdapter;

// Централизованный менеджер ресурсов с кэшированием
class ResourceManager {
public:
    static ResourceManager& getInstance();

    template <typename T>
    std::shared_ptr<Resource<T>> load(const std::string& path) {
        if constexpr (std::is_same_v<T, MeshData>) {
            return loadMesh(path);
        } else if constexpr (std::is_same_v<T, TextureData>) {
            return loadTexture(path);
        } else if constexpr (std::is_same_v<T, ShaderData>) {
            const std::size_t separator = path.find('|');
            if (separator == std::string::npos) {
                return nullptr;
            }

            return loadShader(path.substr(0, separator), path.substr(separator + 1));
        } else {
            static_assert(!std::is_same_v<T, T>, "Unsupported resource type");
        }
    }

    // Инициализация с адаптером рендеринга
    void init(IRenderAdapter* renderer);

    // Загрузка ресурсов с кэшированием
    std::shared_ptr<Resource<MeshData>> loadMesh(const std::string& path);
    std::shared_ptr<Resource<TextureData>> loadTexture(const std::string& path);
    std::shared_ptr<Resource<ShaderData>> loadShader(const std::string& vertexPath, const std::string& fragmentPath);

    // Перезагрузка ресурса (для горячей замены)
    void reloadShader(const std::string& vertexPath, const std::string& fragmentPath);
    void reloadShadersForFile(const std::string& changedPath);

    // Очистка кэша
    void clearCache();
    
    // Получение статистики
    size_t getMeshCount() const { return meshCache_.size(); }
    size_t getTextureCount() const { return textureCache_.size(); }
    size_t getShaderCount() const { return shaderCache_.size(); }
    std::vector<std::string> getMeshIds() const;
    std::vector<std::string> getTextureIds() const;
    std::vector<std::string> getShaderIds() const;
    size_t estimateMemoryUsageBytes() const;

private:
    static std::string makeShaderKey(const std::string& vertexPath, const std::string& fragmentPath);

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
