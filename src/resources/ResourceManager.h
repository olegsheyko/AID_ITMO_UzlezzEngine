#pragma once

#include "Resource.h"
#include "ResourceTypes.h"
#include "jobs/JobSystem.h"

#include <atomic>
#include <cstddef>
#include <deque>
#include <mutex>
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

    // Асинхронная загрузка текстуры: чтение и декодирование — задачей в job system,
    // заливка на GPU — в pumpUploads на главном потоке. Хэндл возвращается сразу;
    // пока он не Ready, рендер рисует заглушку. Звать только с главного потока.
    // nullptr — если менеджер уже останавливается.
    std::shared_ptr<Resource<TextureData>> loadTextureAsync(const std::string& path, JobPriority priority = JobPriority::Normal);

    // Раз в кадр с главного потока: залить на GPU то, что декодировали воркеры, в пределах бюджета кадра.
    void pumpUploads();
    // Не больше maxUploads текстур и maxBytes байт за кадр; одна текстура за кадр проходит всегда.
    void setUploadBudget(std::size_t maxUploads, std::size_t maxBytes);

    // Перед остановкой job system: новые загрузки не принимать, начатые — свернуть.
    void beginShutdown();

    // Выгрузить текстуру, если её держит только кэш. Нужна для повторных прогонов тяжёлой пачки.
    bool releaseTexture(const std::string& path);

    // Шахматная текстура, которую рисуют вместо ещё не загруженной. 0 — если рендер не инициализирован.
    unsigned int placeholderTextureId();

    // Загрузки, поставленные в job system и ещё не залитые на GPU.
    std::size_t pendingLoadCount() const { return static_cast<std::size_t>(pendingLoads_.load(std::memory_order_relaxed)); }

    // Сколько главный поток провёл в загрузке ресурсов с прошлого вызова, мс:
    // синхронные загрузки плюс заливка в pumpUploads.
    double takeMainThreadLoadMs();
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
    std::vector<std::string> getAvailableTexturePaths() const;
    std::vector<std::string> getShaderIds() const;
    size_t estimateMemoryUsageBytes() const;

private:
    static std::string makeShaderKey(const std::string& vertexPath, const std::string& fragmentPath);

    ResourceManager() = default;
    ~ResourceManager() = default;
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    void addMainThreadLoadTime(double ms) { mainThreadLoadMs_ += ms; }
    void drainUploadQueue();

    IRenderAdapter* renderer_ = nullptr;

    // Декодированные воркерами текстуры, ждущие заливки на главном потоке.
    std::mutex uploadMutex_;
    std::deque<std::shared_ptr<Resource<TextureData>>> uploadQueue_;
    std::size_t maxUploadsPerFrame_ = 4;
    std::size_t maxUploadBytesPerFrame_ = 16u * 1024u * 1024u;

    std::atomic<bool> shuttingDown_{false};
    std::atomic<int> pendingLoads_{0};
    double mainThreadLoadMs_ = 0.0;
    unsigned int placeholderTextureId_ = 0;

    // Кэши для разных типов ресурсов
    std::unordered_map<std::string, std::shared_ptr<Resource<MeshData>>> meshCache_;
    std::unordered_map<std::string, std::shared_ptr<Resource<TextureData>>> textureCache_;
    std::unordered_map<std::string, std::shared_ptr<Resource<ShaderData>>> shaderCache_;
};
