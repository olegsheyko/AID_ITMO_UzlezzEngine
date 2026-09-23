#include "bench/StressRun.h"

#include "core/Logger.h"
#include "core/ProcessStats.h"
#include "render/IRenderAdapter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace {
constexpr double kHitchMs = 33.3;
// Первые кадры после старта ещё несут загрузку сцены — в статистику кадров не идут.
constexpr int kWarmupFrames = 60;
// Первые циклы прогревают драйвер и аллокаторы; рост памяти меряем от этого цикла.
constexpr std::size_t kSettledCycle = 10;

double toMb(std::uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

std::string formatMb(std::uint64_t bytes) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f MB", toMb(bytes));
    return text;
}
}

StressRun::StressRun(IRenderAdapter& renderer, StressConfig config)
    : renderer_(renderer), config_(std::move(config)) {
}

StressRun::StressRun(IRenderAdapter& renderer)
    : StressRun(renderer, StressConfig{}) {
}

void StressRun::start() {
    if (running_) {
        return;
    }
    stats_ = StressStats{};
    running_ = true;
    finished_ = false;
    warmupFramesLeft_ = kWarmupFrames;
    startTime_ = std::chrono::steady_clock::now();
    LOG_INFO(config_.durationSeconds > 0.0
        ? "StressRun: started for " + std::to_string(config_.durationSeconds) + " s"
        : std::string("StressRun: started until stopped"));
    beginCycle();
}

void StressRun::stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    LOG_INFO("StressRun: stopped after " + std::to_string(stats_.cycles) + " cycles, " +
        std::to_string(stats_.failedTextures) + " failed textures, " + std::to_string(stats_.hitches) +
        " hitches, worst frame " + std::to_string(stats_.worstFrameMs) + " ms, footprint " + formatMb(stats_.footprintNow));
    if (!config_.summaryPath.empty()) {
        writeSummary();
    }
}

double StressRun::elapsedSeconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime_).count();
}

void StressRun::beginCycle() {
    // Чётные циклы — burst, нечётные — stream. start() сначала выгружает прошлую пачку.
    const LoadScenario::Mode mode = stats_.cycles % 2 == 0 ? LoadScenario::Mode::Burst : LoadScenario::Mode::Stream;
    scenario_.start(mode, true);
    stats_.cycleStartFootprint.push_back(processMemoryFootprintBytes());
    stats_.cycleStartTextures.push_back(renderer_.liveTextureCount());
    phase_ = Phase::Loading;
}

void StressRun::onFrame(double previousFrameMs) {
    if (!running_) {
        return;
    }

    if (warmupFramesLeft_ > 0) {
        --warmupFramesLeft_;
    } else {
        ++stats_.frames;
        if (previousFrameMs > kHitchMs) {
            ++stats_.hitches;
        }
        stats_.worstFrameMs = std::max(stats_.worstFrameMs, previousFrameMs);
    }
    stats_.footprintNow = processMemoryFootprintBytes();
    stats_.footprintPeak = std::max(stats_.footprintPeak, stats_.footprintNow);

    // Срок вышел посреди цикла — и хорошо: заодно проверяется выход с живыми загрузками.
    if (config_.durationSeconds > 0.0 && elapsedSeconds() >= config_.durationSeconds) {
        stop();
        finished_ = true;
        return;
    }

    if (phase_ == Phase::Loading) {
        scenario_.update();
        if (scenario_.isComplete()) {
            ++stats_.cycles;
            stats_.failedTextures += scenario_.failedCount();
            stats_.fromCache += scenario_.fromCacheCount();
            phase_ = Phase::Holding;
            holdFramesLeft_ = config_.holdFrames;
            if (stats_.cycles % 25 == 0) {
                LOG_INFO("StressRun: cycle " + std::to_string(stats_.cycles) + ", footprint at cycle start " +
                    formatMb(stats_.cycleStartFootprint.back()) + ", live textures " +
                    std::to_string(stats_.cycleStartTextures.back()) + ", failed " + std::to_string(stats_.failedTextures) +
                    ", hitches " + std::to_string(stats_.hitches));
            }
        }
    } else if (--holdFramesLeft_ <= 0) {
        beginCycle();
    }
}

void StressRun::writeSummary() const {
    const std::filesystem::path path(config_.summaryPath);
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    std::ofstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("StressRun: failed to write " + config_.summaryPath);
        return;
    }

    const auto& footprint = stats_.cycleStartFootprint;
    const auto& textures = stats_.cycleStartTextures;
    // Сравниваем «прогретый» цикл с последним: память, выросшая между ними, и есть утечка.
    const std::size_t settled = std::min(kSettledCycle, footprint.empty() ? std::size_t{0} : footprint.size() - 1);
    const std::uint64_t settledFootprint = footprint.empty() ? 0 : footprint[settled];
    const std::uint64_t lastFootprint = footprint.empty() ? 0 : footprint.back();
    std::uint64_t bandMin = settledFootprint;
    std::uint64_t bandMax = settledFootprint;
    for (std::size_t i = settled; i < footprint.size(); ++i) {
        bandMin = std::min(bandMin, footprint[i]);
        bandMax = std::max(bandMax, footprint[i]);
    }

    char line[160];
    file << "# uzlezz-stress v1\n";
    std::snprintf(line, sizeof(line), "duration_s=%.1f\n", elapsedSeconds());
    file << line;
    file << "cycles=" << stats_.cycles << "\n"
         << "failed_textures=" << stats_.failedTextures << "\n"
         << "from_cache=" << stats_.fromCache << "\n"
         << "frames=" << stats_.frames << "\n"
         << "hitches_over_33ms=" << stats_.hitches << "\n";
    std::snprintf(line, sizeof(line), "worst_frame_ms=%.2f\n", stats_.worstFrameMs);
    file << line;
    std::snprintf(line, sizeof(line),
        "footprint_cycle_start_mb first=%.1f settled=%.1f(cycle %zu) last=%.1f band=%.1f..%.1f growth_since_settled=%.1f\n",
        footprint.empty() ? 0.0 : toMb(footprint.front()), toMb(settledFootprint), settled + 1, toMb(lastFootprint),
        toMb(bandMin), toMb(bandMax), toMb(lastFootprint) - toMb(settledFootprint));
    file << line;
    std::snprintf(line, sizeof(line), "footprint_peak_mb=%.1f\n", toMb(stats_.footprintPeak));
    file << line;

    // Наклон линейного тренда памяти по второй половине циклов: кэши драйвера и аллокатора
    // выходят на плато примерно к сотому-полуторасотому циклу, а утечка растёт всё время.
    // Короткий прогон (меньше ~300 циклов) ещё захватывает прогрев — наклон будет завышен.
    const std::size_t trendStart = std::max(settled, footprint.size() / 2);
    if (footprint.size() > trendStart + 2) {
        double meanX = 0.0;
        double meanY = 0.0;
        const std::size_t n = footprint.size() - trendStart;
        for (std::size_t i = trendStart; i < footprint.size(); ++i) {
            meanX += static_cast<double>(i);
            meanY += toMb(footprint[i]);
        }
        meanX /= static_cast<double>(n);
        meanY /= static_cast<double>(n);
        double covariance = 0.0;
        double variance = 0.0;
        for (std::size_t i = trendStart; i < footprint.size(); ++i) {
            covariance += (static_cast<double>(i) - meanX) * (toMb(footprint[i]) - meanY);
            variance += (static_cast<double>(i) - meanX) * (static_cast<double>(i) - meanX);
        }
        double scatter = 0.0;
        const double slope = variance > 0.0 ? covariance / variance : 0.0;
        for (std::size_t i = trendStart; i < footprint.size(); ++i) {
            const double residual = toMb(footprint[i]) - (meanY + slope * (static_cast<double>(i) - meanX));
            scatter += residual * residual;
        }
        std::snprintf(line, sizeof(line), "footprint_trend_mb_per_100_cycles=%+.2f scatter_mb=%.1f cycles=%zu..%zu\n",
            slope * 100.0, std::sqrt(scatter / static_cast<double>(n)), trendStart + 1, footprint.size());
        file << line;
    }
    file << "live_textures_cycle_start first=" << (textures.empty() ? 0 : textures.front())
         << " last=" << (textures.empty() ? 0 : textures.back())
         << " max=" << (textures.empty() ? 0 : *std::max_element(textures.begin(), textures.end())) << "\n";
    LOG_INFO("StressRun: summary written to " + config_.summaryPath);
}
