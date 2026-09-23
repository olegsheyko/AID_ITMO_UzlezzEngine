#include "jobs/JobSystem.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

JobSystem& jobs() {
    JobSystem& system = JobSystem::getInstance();
    if (!system.isRunning()) {
        system.init(JobSystem::Config{4});
    }
    return system;
}

void startsRequestedWorkers() {
    require(jobs().workerCount() == 4, "expected 4 worker threads");
}

void submitRunsEveryJob() {
    std::atomic<int> counter{0};
    std::vector<JobHandle> handles;
    for (int i = 0; i < 1000; ++i) {
        handles.push_back(jobs().submit([&counter] { counter.fetch_add(1, std::memory_order_relaxed); }));
    }
    for (const JobHandle& handle : handles) {
        jobs().wait(handle);
        require(handle.isDone(), "waited job must be done");
    }
    require(counter.load() == 1000, "every submitted job must run exactly once");
}

void parallelForVisitsEveryIndexOnce() {
    constexpr uint32_t kCount = 100000;
    std::vector<std::atomic<int>> visits(kCount);
    jobs().parallelFor(kCount, [&visits](uint32_t begin, uint32_t end) {
        for (uint32_t i = begin; i < end; ++i) {
            visits[i].fetch_add(1, std::memory_order_relaxed);
        }
    }, 64);
    for (uint32_t i = 0; i < kCount; ++i) {
        require(visits[i].load() == 1, "parallelFor must visit every index exactly once");
    }
}

void jobsCanSubmitAndWaitForJobs() {
    std::atomic<int> inner{0};
    JobHandle outer = jobs().submit([&inner] {
        std::vector<JobHandle> children;
        for (int i = 0; i < 10; ++i) {
            children.push_back(JobSystem::getInstance().submit([&inner] { inner.fetch_add(1); }));
        }
        for (const JobHandle& child : children) {
            JobSystem::getInstance().wait(child);
        }
    }, JobPriority::High);
    jobs().wait(outer);
    require(inner.load() == 10, "nested jobs must finish before their parent returns");
}

void everyPriorityRuns() {
    std::atomic<int> ran{0};
    std::vector<JobHandle> handles;
    for (JobPriority priority : {JobPriority::Low, JobPriority::Normal, JobPriority::High}) {
        handles.push_back(jobs().submit([&ran] { ran.fetch_add(1); }, priority));
    }
    for (const JobHandle& handle : handles) {
        jobs().wait(handle);
    }
    require(ran.load() == 3, "jobs of every priority must run");
}

void throwingJobKeepsWorkersAlive() {
    jobs().wait(jobs().submit([] { throw std::runtime_error("expected test exception"); }));
    std::atomic<bool> ranAfter{false};
    jobs().wait(jobs().submit([&ranAfter] { ranAfter = true; }));
    require(ranAfter.load(), "workers must keep running after a job throws");
}

void collectCompletedReleasesFinishedJobs() {
    {
        std::vector<JobHandle> handles;
        for (int i = 0; i < 50; ++i) {
            handles.push_back(jobs().submit([] {}));
        }
        for (const JobHandle& handle : handles) {
            jobs().wait(handle);
        }
    }
    jobs().collectCompleted();
    require(jobs().inFlightCount() == 0, "collectCompleted must release every finished job");
}

void shutdownWaitsForPendingJobs() {
    std::atomic<int> finished{0};
    for (int i = 0; i < 100; ++i) {
        jobs().submit([&finished] {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            finished.fetch_add(1);
        }, JobPriority::Low);
    }
    // Выход приложения с живыми задачами: shutdown обязан дождаться их, а не бросить на полпути.
    JobSystem::getInstance().shutdown();
    require(!JobSystem::getInstance().isRunning(), "job system must be stopped after shutdown");
    require(finished.load() == 100, "shutdown must wait for every pending job");
}

void submitAfterShutdownRunsInline() {
    JobSystem& system = JobSystem::getInstance();
    system.shutdown();
    const std::thread::id caller = std::this_thread::get_id();
    std::thread::id ranOn;
    JobHandle handle = system.submit([&ranOn] { ranOn = std::this_thread::get_id(); });
    require(handle.isDone(), "job submitted to a stopped system must already be done");
    require(ranOn == caller, "job submitted to a stopped system must run on the calling thread");
}

void restartsAfterShutdown() {
    JobSystem& system = JobSystem::getInstance();
    system.shutdown();
    require(system.init(JobSystem::Config{2}), "init after shutdown must succeed");
    require(system.workerCount() == 2, "restarted system must use the new worker count");
    std::atomic<int> counter{0};
    system.wait(system.submit([&counter] { counter = 1; }));
    require(counter.load() == 1, "restarted system must run jobs");
    system.shutdown();
}
}

int main() {
    int failures = 0;
    auto run = [&failures](const char* name, auto test) {
        try {
            test();
            std::cout << "PASS: " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
        }
    };
    run("starts the requested number of workers", startsRequestedWorkers);
    run("submit runs every job exactly once", submitRunsEveryJob);
    run("parallelFor visits every index once", parallelForVisitsEveryIndexOnce);
    run("jobs can submit and wait for jobs", jobsCanSubmitAndWaitForJobs);
    run("jobs of every priority run", everyPriorityRuns);
    run("a throwing job keeps workers alive", throwingJobKeepsWorkersAlive);
    run("collectCompleted releases finished jobs", collectCompletedReleasesFinishedJobs);
    run("shutdown waits for pending jobs", shutdownWaitsForPendingJobs);
    run("submit after shutdown runs inline", submitAfterShutdownRunsInline);
    run("restart after shutdown", restartsAfterShutdown);
    return failures == 0 ? 0 : 1;
}
