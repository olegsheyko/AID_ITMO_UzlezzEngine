#include "jobs/JobSystem.h"

#include "core/Logger.h"

#include <enkiTS/TaskScheduler.h>
#include <tracy/Tracy.hpp>
#ifdef TRACY_ENABLE
#include <tracy/TracyC.h>
#endif

#include <algorithm>
#include <cstdio>
#include <exception>
#include <string>
#include <utility>

namespace jobs_detail {
struct Job final : enki::ITaskSet {
    explicit Job(std::function<void()> function)
        : function_(std::move(function)) {
    }

    void ExecuteRange(enki::TaskSetPartition, uint32_t) override {
        run();
    }

    void run() {
        // Исключение в задаче не должно ронять воркер, а с ним и весь процесс.
        try {
            function_();
        } catch (const std::exception& error) {
            LOG_ERROR(std::string("JobSystem: job threw: ") + error.what());
        } catch (...) {
            LOG_ERROR("JobSystem: job threw an unknown exception");
        }
    }

    std::function<void()> function_;
};
}

namespace {
enki::TaskPriority toEnki(JobPriority priority) {
    switch (priority) {
    case JobPriority::High:
        return enki::TASK_PRIORITY_HIGH;
    case JobPriority::Normal:
        return enki::TASK_PRIORITY_MED;
    default:
        return enki::TASK_PRIORITY_LOW;
    }
}

#ifdef TRACY_ENABLE
// Колбэки enkiTS приходят парами начало/конец на одном потоке, поэтому хватает thread_local.
thread_local TracyCZoneCtx tlWaitZone;
thread_local TracyCZoneCtx tlSleepZone;
const ___tracy_source_location_data kWaitLocation{"Job wait", "enkiTS", __FILE__, __LINE__, 0xC0A060};
const ___tracy_source_location_data kSleepLocation{"Worker sleep", "enkiTS", __FILE__, __LINE__, 0x505050};

void onThreadStart(uint32_t threadNum) {
    char name[32];
    std::snprintf(name, sizeof(name), "Job worker %u", threadNum);
    tracy::SetThreadName(name);
}

void onWaitStart(uint32_t) {
    tlWaitZone = ___tracy_emit_zone_begin(&kWaitLocation, 1);
}

void onWaitStop(uint32_t) {
    ___tracy_emit_zone_end(tlWaitZone);
}

void onSleepStart(uint32_t) {
    tlSleepZone = ___tracy_emit_zone_begin(&kSleepLocation, 1);
}

void onSleepStop(uint32_t) {
    ___tracy_emit_zone_end(tlSleepZone);
}
#endif
}

JobHandle::JobHandle(std::shared_ptr<jobs_detail::Job> job)
    : job_(std::move(job)) {
}

bool JobHandle::isDone() const {
    return !job_ || job_->GetIsComplete();
}

JobSystem& JobSystem::getInstance() {
    static JobSystem instance;
    return instance;
}

JobSystem::JobSystem() = default;

JobSystem::~JobSystem() {
    // Страховка на случай выхода без Application::shutdown; логгер к этому моменту может быть уже разрушен.
    if (running_.load(std::memory_order_acquire) && scheduler_) {
        scheduler_->WaitforAllAndShutdown();
    }
}

bool JobSystem::init(const Config& config) {
    if (isRunning()) {
        return true;
    }

    enki::TaskSchedulerConfig schedulerConfig;
    if (config.workerThreads > 0) {
        schedulerConfig.numTaskThreadsToCreate = config.workerThreads;
    }
    if (schedulerConfig.numTaskThreadsToCreate == 0) {
        // Одноядерная машина: без воркеров задачи всё равно выполнятся в wait/parallelFor.
        schedulerConfig.numTaskThreadsToCreate = 1;
    }
#ifdef TRACY_ENABLE
    schedulerConfig.profilerCallbacks.threadStart = onThreadStart;
    schedulerConfig.profilerCallbacks.waitForTaskCompleteStart = onWaitStart;
    schedulerConfig.profilerCallbacks.waitForTaskCompleteStop = onWaitStop;
    schedulerConfig.profilerCallbacks.waitForNewTaskSuspendStart = onSleepStart;
    schedulerConfig.profilerCallbacks.waitForNewTaskSuspendStop = onSleepStop;
#endif

    scheduler_ = std::make_unique<enki::TaskScheduler>();
    scheduler_->Initialize(schedulerConfig);
    running_.store(true, std::memory_order_release);
    LOG_INFO("JobSystem: started " + std::to_string(workerCount()) + " worker threads");
    return true;
}

void JobSystem::shutdown() {
    if (!isRunning()) {
        return;
    }

    ZoneScopedN("JobSystem shutdown");
    // Задачи, поставленные изнутри других задач, тоже дождутся: WaitforAll ждёт до пустых очередей.
    scheduler_->WaitforAllAndShutdown();
    running_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(inFlightMutex_);
        inFlight_.clear();
    }
    scheduler_.reset();
    LOG_INFO("JobSystem: stopped");
}

uint32_t JobSystem::workerCount() const {
    // GetNumTaskThreads считает и поток, который создал планировщик.
    return scheduler_ ? scheduler_->GetNumTaskThreads() - 1 : 0;
}

JobHandle JobSystem::submit(std::function<void()> job, JobPriority priority) {
    auto task = std::make_shared<jobs_detail::Job>(std::move(job));
    if (!isRunning()) {
        task->run();
        return JobHandle(task);
    }

    task->m_Priority = toEnki(priority);
    // Сначала в очередь, потом в список: пока задача в очереди, её держит локальный shared_ptr,
    // а collectCompleted не может принять ещё не поставленную задачу за выполненную.
    scheduler_->AddTaskSetToPipe(task.get());
    {
        std::lock_guard<std::mutex> lock(inFlightMutex_);
        inFlight_.push_back(task);
    }
    return JobHandle(task);
}

void JobSystem::wait(const JobHandle& handle) {
    if (!handle.job_ || !isRunning()) {
        return;
    }
    scheduler_->WaitforTask(handle.job_.get());
}

void JobSystem::parallelFor(uint32_t count, const std::function<void(uint32_t begin, uint32_t end)>& func,
    uint32_t minRange, JobPriority priority) {
    if (count == 0) {
        return;
    }
    if (!isRunning()) {
        func(0, count);
        return;
    }

    enki::TaskSet task(count, [&func](enki::TaskSetPartition range, uint32_t) {
        func(range.start, range.end);
    });
    task.m_MinRange = std::max<uint32_t>(1, minRange);
    task.m_Priority = toEnki(priority);
    scheduler_->AddTaskSetToPipe(&task);
    scheduler_->WaitforTask(&task);
}

void JobSystem::collectCompleted() {
    std::lock_guard<std::mutex> lock(inFlightMutex_);
    inFlight_.erase(std::remove_if(inFlight_.begin(), inFlight_.end(), [](const auto& task) {
        // TaskComplete в enkiTS не трогает задачу после обнуления счётчика — удалять безопасно.
        return task->GetIsComplete();
    }), inFlight_.end());
    TracyPlot("Jobs in flight", static_cast<int64_t>(inFlight_.size()));
}

std::size_t JobSystem::inFlightCount() const {
    std::lock_guard<std::mutex> lock(inFlightMutex_);
    return inFlight_.size();
}
