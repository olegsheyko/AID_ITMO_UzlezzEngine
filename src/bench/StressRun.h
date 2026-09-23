#pragma once

#include "bench/LoadScenario.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class IRenderAdapter;

struct StressConfig {
    // Сколько секунд гонять; 0 — пока не остановят (режим редактора).
    double durationSeconds = 0.0;
    // Куда записать итог; пусто — только в лог.
    std::string summaryPath;
    // Сколько кадров держать загруженную пачку перед выгрузкой.
    int holdFrames = 30;
};

struct StressStats {
    std::size_t cycles = 0;
    std::size_t failedTextures = 0;
    std::size_t fromCache = 0;
    std::size_t frames = 0;
    std::size_t hitches = 0;  // кадров длиннее 33 мс
    double worstFrameMs = 0.0;
    std::uint64_t footprintNow = 0;
    std::uint64_t footprintPeak = 0;
    // Память и живые GPU-текстуры в начале каждого цикла, когда прошлая пачка уже выгружена.
    std::vector<std::uint64_t> cycleStartFootprint;
    std::vector<std::size_t> cycleStartTextures;
};

// Стресс для блока стабильности ЛР 1: тяжёлая пачка грузится и выгружается по кругу,
// burst и stream по очереди, через job system. Утечка видна как рост памяти или числа
// живых GPU-текстур в начале цикла — в этой точке все циклы должны быть одинаковыми.
class StressRun {
public:
    StressRun(IRenderAdapter& renderer, StressConfig config);
    explicit StressRun(IRenderAdapter& renderer);

    void start();
    // Остановить и записать итог; незавершённый цикл не считается.
    void stop();
    // Раз в кадр на главном потоке: время прошлого кадра, мс.
    void onFrame(double previousFrameMs);

    bool isRunning() const { return running_; }
    // Истёк срок — приложению пора выходить. Бывает только при durationSeconds > 0.
    bool isFinished() const { return finished_; }
    const StressStats& stats() const { return stats_; }
    double elapsedSeconds() const;

private:
    enum class Phase { Loading, Holding };

    void beginCycle();
    void writeSummary() const;

    IRenderAdapter& renderer_;
    StressConfig config_;
    StressStats stats_;
    LoadScenario scenario_;
    Phase phase_ = Phase::Loading;
    int holdFramesLeft_ = 0;
    int warmupFramesLeft_ = 0;
    bool running_ = false;
    bool finished_ = false;
    std::chrono::steady_clock::time_point startTime_{};
};
