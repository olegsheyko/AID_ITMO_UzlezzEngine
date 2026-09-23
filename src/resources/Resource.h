#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

// Жизненный цикл ресурса. Синхронная загрузка сразу ставит Ready или Failed,
// асинхронная проходит все шаги: воркер декодирует, главный поток заливает на GPU.
enum class ResourceState : std::uint8_t {
    Queued,          // поставлен в job system
    Decoding,        // воркер читает и декодирует файл
    ReadyForUpload,  // данные на CPU, ждёт пампа на главном потоке
    Ready,           // залит на GPU, можно рисовать
    Failed
};

// Базовый класс для всех ресурсов
class IResource {
public:
    virtual ~IResource() = default;

    const std::string& getPath() const { return path_; }
    ResourceState getState() const { return state_.load(std::memory_order_acquire); }
    bool isLoaded() const { return getState() == ResourceState::Ready; }
    bool isFailed() const { return getState() == ResourceState::Failed; }
    bool isPending() const {
        const ResourceState state = getState();
        return state != ResourceState::Ready && state != ResourceState::Failed;
    }

    // release в паре с acquire в getState: всё, что записано в данные ресурса до смены
    // состояния, видит поток, который это состояние прочитал.
    void setState(ResourceState state) { state_.store(state, std::memory_order_release); }

protected:
    std::string path_;
    std::atomic<ResourceState> state_{ResourceState::Queued};
};

// Шаблонная обёртка для типизированных ресурсов
template<typename T>
class Resource : public IResource {
public:
    Resource() = default;
    explicit Resource(const std::string& path) {
        path_ = path;
    }

    T* getData() { return &data_; }
    const T* getData() const { return &data_; }

    void setLoaded(bool loaded) { setState(loaded ? ResourceState::Ready : ResourceState::Failed); }

private:
    T data_;
};
