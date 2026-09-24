#include "ResourceManager.h"
#include "MeshLoader.h"
#include "TextureLoader.h"
#include "ShaderLoader.h"
#include "core/Logger.h"
#include "render/IRenderAdapter.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <utility>
#include <stdexcept>
#include <vector>

namespace {
// Меряет, сколько главный поток провёл в загрузке — для колонки main_load_ms бенчмарка.
class MainThreadLoadTimer {
public:
    explicit MainThreadLoadTimer(double& accumulatorMs)
        : accumulatorMs_(accumulatorMs), start_(std::chrono::steady_clock::now()) {
    }
    ~MainThreadLoadTimer() {
        accumulatorMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
    }

private:
    double& accumulatorMs_;
    std::chrono::steady_clock::time_point start_;
};
}

std::string ResourceManager::makeShaderKey(const std::string& vertexPath, const std::string& fragmentPath) {
    return vertexPath + "|" + fragmentPath;
}

ResourceManager& ResourceManager::getInstance() {
    static ResourceManager instance;
    return instance;
}

void ResourceManager::init(IRenderAdapter* renderer) {
    renderer_ = renderer;
    shuttingDown_.store(false, std::memory_order_release);
    LOG_INFO("ResourceManager initialized");
}

std::shared_ptr<Resource<MeshData>> ResourceManager::loadMesh(const std::string& path) {
    // Проверяем кэш
    auto it = meshCache_.find(path);
    if (it != meshCache_.end()) {
        return it->second;
    }

    // Загружаем новый меш
    MainThreadLoadTimer timer(mainThreadLoadMs_);
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
        // Провалившаяся асинхронная загрузка остаётся в кэше как Failed; синхронный контракт — nullptr.
        // Ещё идущая асинхронная возвращается как есть: вызывающий видит её состояние.
        if (it->second->isFailed()) {
            return nullptr;
        }
        LOG_INFO("Texture loaded from cache: " + path);
        return it->second;
    }

    MainThreadLoadTimer timer(mainThreadLoadMs_);
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

    MainThreadLoadTimer timer(mainThreadLoadMs_);
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

// Workers own CPU data until publishing a finalization task. Only the main thread
// touches caches, GPU objects, material handles, or publishes Ready to consumers.
template<class T, class Decode, class Finalize, class Discard>
void ResourceManager::startAsync(std::shared_ptr<Resource<T>> resource, Decode decode,
    Finalize finalize, Discard discard, JobPriority priority) {
    pendingLoads_.fetch_add(1, std::memory_order_relaxed);
    auto cancel = [this, resource, discard] {
        discard(*resource->getData());
        resource->setState(ResourceState::Failed);
        pendingLoads_.fetch_sub(1, std::memory_order_relaxed);
    };
    const auto job = JobSystem::getInstance().submitBackground([this, resource, decode, finalize, cancel] {
        bool decoded = false;
        try {
            if (!shuttingDown_.load(std::memory_order_acquire)) {
                resource->setState(ResourceState::Decoding);
                decoded = decode(*resource->getData());
            }
        } catch (const std::exception& error) {
            LOG_ERROR("Resource decode failed: " + resource->getPath() + ": " + error.what());
        } catch (...) {
            LOG_ERROR("Resource decode failed: " + resource->getPath());
        }
        if (decoded) resource->setState(ResourceState::ReadyForUpload);
        Upload upload;
        upload.cancel = cancel;
        upload.step = [this, resource, finalize, cancel, decoded]() mutable {
            if (!decoded || shuttingDown_.load(std::memory_order_acquire)) { cancel(); return true; }
            if (!finalize(*resource->getData())) return false;
            resource->setState(ResourceState::Ready);
            pendingLoads_.fetch_sub(1, std::memory_order_relaxed);
            return true;
        };
        std::lock_guard<std::mutex> lock(uploadMutex_);
        uploadQueue_.push_back(std::move(upload));
    }, priority);
    if (!job.isValid()) cancel();
}

void ResourceManager::discardMesh(MeshData& mesh) {
    for (auto& sub : mesh.subMeshes) {
        if (renderer_ && (sub.vao || sub.vbo || sub.ebo)) renderer_->destroyMesh(sub.vao, sub.vbo, sub.ebo);
    }
    mesh = {};
}

std::shared_ptr<Resource<MeshData>> ResourceManager::loadMeshAsync(const std::string& path, JobPriority priority) {
    if (shuttingDown_.load(std::memory_order_acquire)) return nullptr;
    if (path == "primitive:cube") return loadMesh(path); // Generated locally; no file to read.
    if (auto it = meshCache_.find(path); it != meshCache_.end()) return it->second;
    auto resource = std::make_shared<Resource<MeshData>>(path);
    meshCache_[path] = resource;
    startAsync(resource, [path](MeshData& data) { return MeshLoader::decode(path, data); },
        [this, priority, next = size_t{0}](MeshData& data) mutable {
            ZoneScopedN("Mesh GPU upload chunk");
            if (!renderer_ || data.subMeshes.empty()) throw std::runtime_error("Mesh has no GPU data");
            // One submesh per pump step; texture decoding uses the same Job System.
            auto& sub = data.subMeshes[next];
            if (!sub.material.diffuseTexturePath.empty())
                sub.material.cachedDiffuseTexture = loadTextureAsync(sub.material.diffuseTexturePath, priority);
            if (!MeshLoader::uploadSubMeshToGPU(sub, renderer_)) throw std::runtime_error("Mesh GPU upload failed");
            if (++next < data.subMeshes.size()) return false;
            if (data.subMeshes.size() == 1) {
                data.vao = sub.vao; data.vbo = sub.vbo; data.ebo = sub.ebo; data.indexCount = sub.indexCount;
            }
            return true;
        }, [this](MeshData& data) { discardMesh(data); }, priority);
    return resource;
}

std::shared_ptr<Resource<SceneManifest>> ResourceManager::loadSceneAsync(const std::string& path, JobPriority priority) {
    if (shuttingDown_.load(std::memory_order_acquire)) return nullptr;
    if (auto it = sceneCache_.find(path); it != sceneCache_.end()) return it->second;
    auto resource = std::make_shared<Resource<SceneManifest>>(path);
    sceneCache_[path] = resource;
    startAsync(resource, [path](SceneManifest& data) {
        ZoneScopedN("Scene read and parse");
        return data.loadFromFile(path);
    }, [](SceneManifest&) { return true; }, [](SceneManifest& data) { data = {}; }, priority);
    return resource;
}

std::shared_ptr<Resource<TextureData>> ResourceManager::loadTextureAsync(const std::string& path, JobPriority priority) {
    if (shuttingDown_.load(std::memory_order_acquire)) return nullptr;
    if (auto it = textureCache_.find(path); it != textureCache_.end()) return it->second;
    auto resource = std::make_shared<Resource<TextureData>>(path);
    textureCache_[path] = resource;
    startAsync(resource, [path](TextureData& data) { return TextureLoader::decode(path, data); },
        [this, path](TextureData& data) {
            if (!TextureLoader::upload(data, renderer_, path)) throw std::runtime_error("Texture GPU upload failed: " + path);
            return true;
        }, [](TextureData& data) { std::free(data.pixels); data.pixels = nullptr; }, priority);
    return resource;
}

void ResourceManager::pumpUploads() {
    ZoneScopedN("Resource upload pump");
    const auto start = std::chrono::steady_clock::now();
    const auto elapsedMs = [&start] {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };
    std::size_t uploads = 0;

    while (uploads < maxUploadsPerFrame_) {
        // Первый шаг проходит всегда; бюджет проверяется между неделимыми GPU-вызовами.
        if (uploads > 0 && elapsedMs() >= maxUploadMsPerFrame_) {
            break;
        }
        Upload next;
        {
            std::lock_guard<std::mutex> lock(uploadMutex_);
            if (uploadQueue_.empty()) {
                break;
            }
            next = std::move(uploadQueue_.front());
            uploadQueue_.pop_front();
        }

        try {
            if (!next.step()) {
                std::lock_guard<std::mutex> lock(uploadMutex_);
                uploadQueue_.push_back(std::move(next));
            }
        } catch (const std::exception& error) {
            LOG_ERROR(std::string("Resource finalization failed: ") + error.what());
            next.cancel();
        }
        ++uploads;
    }

    if (uploads > 0) {
        addMainThreadLoadTime(elapsedMs());
    }
    TracyPlot("Loads pending", static_cast<int64_t>(pendingLoads_.load(std::memory_order_relaxed)));
}

void ResourceManager::setUploadBudget(double maxMs, std::size_t maxUploads) {
    maxUploadMsPerFrame_ = std::max(0.0, maxMs);
    maxUploadsPerFrame_ = std::max<std::size_t>(1, maxUploads);
}

void ResourceManager::beginShutdown() {
    shuttingDown_.store(true, std::memory_order_release);
    LOG_INFO("ResourceManager: shutting down, " + std::to_string(pendingLoadCount()) + " loads pending");
}

bool ResourceManager::releaseTexture(const std::string& path) {
    auto it = textureCache_.find(path);
    if (it == textureCache_.end()) {
        return false;
    }
    // Грузится — держат задача и очередь; держит кто-то кроме кэша — ещё нужна. В обоих случаях не трогаем.
    if (it->second->isPending() || it->second.use_count() > 1) {
        return false;
    }
    if (renderer_ != nullptr && it->second->isLoaded()) {
        renderer_->destroyTexture(it->second->getData()->textureId);
    }
    textureCache_.erase(it);
    return true;
}

unsigned int ResourceManager::placeholderTextureId() {
    if (placeholderTextureId_ == 0 && renderer_ != nullptr) {
        // Серая шахматка 8×8 клеток: видно, что текстура ещё грузится, и не путается с ошибкой.
        constexpr int kSize = 64;
        constexpr int kCell = 8;
        std::vector<unsigned char> pixels(static_cast<std::size_t>(kSize * kSize * 4));
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                const unsigned char value = ((x / kCell + y / kCell) % 2 == 0) ? 170 : 110;
                unsigned char* pixel = &pixels[static_cast<std::size_t>((y * kSize + x) * 4)];
                pixel[0] = pixel[1] = pixel[2] = value;
                pixel[3] = 255;
            }
        }
        renderer_->createTexture(kSize, kSize, 4, pixels.data(), placeholderTextureId_);
    }
    return placeholderTextureId_;
}

double ResourceManager::takeMainThreadLoadMs() {
    const double ms = mainThreadLoadMs_;
    mainThreadLoadMs_ = 0.0;
    return ms;
}

void ResourceManager::drainUploadQueue() {
    std::deque<Upload> pending;
    {
        std::lock_guard<std::mutex> lock(uploadMutex_);
        pending.swap(uploadQueue_);
    }
    // JobSystem must already be stopped: no worker can publish after this drain.
    for (auto& upload : pending) upload.cancel();
}

void ResourceManager::clearCache() {
    drainUploadQueue();
    if (renderer_ != nullptr && placeholderTextureId_ != 0) {
        renderer_->destroyTexture(placeholderTextureId_);
    }

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

    placeholderTextureId_ = 0;
    sceneCache_.clear();
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
        // Поля текстуры пишет воркер, пока она грузится; читать их можно только у готовой.
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
