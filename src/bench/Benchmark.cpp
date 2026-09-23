#include "bench/Benchmark.h"

#include "core/Logger.h"

#include <tracy/Tracy.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace {
// Как грузятся ресурсы в этой сборке — попадает в CSV, чтобы прогоны «до» и «после» не перепутать.
constexpr const char* kLoadingMode = "sync";
}

Benchmark::Benchmark(Config config)
    : config_(std::move(config)) {
    samples_.reserve(static_cast<std::size_t>(config_.idleFrames + config_.tailFrames) + 4096);
}

void Benchmark::onSceneReady() {
    if (phase_ == Phase::WaitingForScene) {
        LOG_INFO("Benchmark: scene ready, warming up for " + std::to_string(config_.warmupFrames) + " frames");
        enter(Phase::Warmup);
    }
}

void Benchmark::beginFrame(double previousFrameMs) {
    if (phase_ == Phase::WaitingForScene || phase_ == Phase::Finished) {
        return;
    }

    if (currentRecorded_) {
        samples_.push_back({currentPhase_, previousFrameMs, currentLoadMs_});
    }

    // Переходы — по числу кадров, прожитых в текущей фазе; загрузка — пока пачка не готова.
    if (phase_ == Phase::Warmup && phaseFrames_ >= config_.warmupFrames) {
        enter(Phase::Idle);
    } else if (phase_ == Phase::Idle && phaseFrames_ >= config_.idleFrames) {
        enter(Phase::Load);
        TracyMessageL("Heavy batch start");
        scenario_.start(config_.mode);
    } else if (phase_ == Phase::Load && scenario_.isComplete()) {
        enter(Phase::Tail);
    } else if (phase_ == Phase::Load && scenario_.elapsedMs() > config_.loadTimeoutMs) {
        LOG_ERROR("Benchmark: batch did not finish in " + std::to_string(config_.loadTimeoutMs) + " ms");
        finish(false);
        return;
    } else if (phase_ == Phase::Tail && phaseFrames_ >= config_.tailFrames) {
        finish(true);
        return;
    }

    currentPhase_ = phase_;
    currentRecorded_ = phase_ != Phase::Warmup;
    currentLoadMs_ = phase_ == Phase::Load ? scenario_.update() : 0.0;
    ++phaseFrames_;
}

const char* Benchmark::phaseName(Phase phase) {
    switch (phase) {
    case Phase::Idle:
        return "idle";
    case Phase::Load:
        return "load";
    case Phase::Tail:
        return "tail";
    default:
        return "other";
    }
}

void Benchmark::enter(Phase phase) {
    phase_ = phase;
    phaseFrames_ = 0;
}

void Benchmark::finish(bool complete) {
    phase_ = Phase::Finished;
    if (writeCsv(complete)) {
        LOG_INFO("Benchmark: " + std::to_string(samples_.size()) + " frames written to " + config_.outputPath);
    } else {
        LOG_ERROR("Benchmark: failed to write " + config_.outputPath);
    }
}

bool Benchmark::writeCsv(bool complete) const {
    const std::filesystem::path path(config_.outputPath);
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }

    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    file << "# uzlezz-bench v1\n"
         << "# scenario=" << LoadScenario::modeName(config_.mode) << "\n"
         << "# loading=" << kLoadingMode << "\n"
         << "# vsync=0\n"
         << "# warmup_frames=" << config_.warmupFrames << "\n"
         << "# batch_textures=" << scenario_.totalCount() << "\n"
         << "# batch_failed=" << scenario_.failedCount() << "\n"
         << "# batch_bytes=" << scenario_.batchBytes() << "\n"
         << "# batch_ms=" << scenario_.elapsedMs() << "\n"
         << "# complete=" << (complete ? 1 : 0) << "\n"
         << "frame,phase,frame_ms,main_load_ms\n";

    char line[96];
    for (std::size_t i = 0; i < samples_.size(); ++i) {
        const Sample& sample = samples_[i];
        std::snprintf(line, sizeof(line), "%zu,%s,%.4f,%.4f\n", i, phaseName(sample.phase), sample.frameMs, sample.mainLoadMs);
        file << line;
    }
    return static_cast<bool>(file);
}
