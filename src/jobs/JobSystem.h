#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace enki {
class TaskScheduler;
class ICompletable;
}

namespace jobs_detail {
struct Job;
}

enum class JobPriority {
    High,    // нужно в ближайших кадрах: работа кадра, ресурсы сцены
    Normal,
    Low      // фон, который может подождать
};

class JobHandle {
public:
    JobHandle() = default;

    bool isValid() const { return static_cast<bool>(job_); }
    // Задача выполнена; пустой хэндл считается выполненным.
    bool isDone() const;

private:
    friend class JobSystem;
    explicit JobHandle(std::shared_ptr<enki::ICompletable> job);

    std::shared_ptr<enki::ICompletable> job_;
};

// Единственная система фоновой работы движка — поверх enkiTS.
// Запускается и останавливается в Application, задачи ставятся только через неё.
class JobSystem {
public:
    struct Config {
        // 0 — по числу аппаратных потоков минус главный.
        uint32_t workerThreads = 0;
    };

    static JobSystem& getInstance();

    // Перегрузка вместо `= {}`: clang не принимает аргумент по умолчанию из вложенной
    // структуры с инициализаторами полей, пока объемлющий класс не определён до конца.
    bool init() { return init(Config{}); }
    bool init(const Config& config);
    // Дожидается всех задач, включая поставленные изнутри других задач, и останавливает воркеры.
    void shutdown();
    bool isRunning() const { return running_.load(std::memory_order_acquire); }
    uint32_t workerCount() const;

    // Поставить задачу. Можно звать с главного потока и изнутри других задач.
    // Если система не запущена, задача выполняется сразу на вызывающем потоке.
    JobHandle submit(std::function<void()> job, JobPriority priority = JobPriority::Normal);

    // Resource I/O must never be stolen by the main thread during an animation wait.
    // Uses the same enkiTS workers; returns an invalid handle if not initialized.
    JobHandle submitBackground(std::function<void()> job, JobPriority priority = JobPriority::Normal);

    // Дождаться задачи. Пока ждёт, поток сам выполняет задачи из очереди.
    void wait(const JobHandle& handle);

    // Разбить [0, count) на диапазоны не короче minRange и выполнить их параллельно.
    // Возвращается, когда выполнены все диапазоны.
    void parallelFor(uint32_t count, const std::function<void(uint32_t begin, uint32_t end)>& func,
        uint32_t minRange = 1, JobPriority priority = JobPriority::High);

    // Раз в кадр с главного потока: освободить выполненные задачи.
    void collectCompleted();

    // Поставлено, но ещё не собрано collectCompleted.
    std::size_t inFlightCount() const;

private:
    JobSystem();
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    std::unique_ptr<enki::TaskScheduler> scheduler_;
    std::atomic<bool> running_{false};
    std::atomic<uint32_t> nextBackgroundWorker_{0};
    mutable std::mutex inFlightMutex_;
    std::vector<std::shared_ptr<enki::ICompletable>> inFlight_;
};
