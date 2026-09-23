#pragma once

#include "bench/LoadScenario.h"

#include <string>
#include <vector>

// Воспроизводимый прогон для замеров ЛР 1:
// прогрев → простой → сценарий загрузки → хвост → CSV → выход из приложения.
class Benchmark {
public:
    struct Config {
        LoadScenario::Mode mode = LoadScenario::Mode::Burst;
        // true — через job system, false — прежняя синхронная загрузка на главном потоке.
        bool asyncLoading = true;
        // Проверка шатдауна: выйти через кадр после запуска пачки, пока воркеры её декодируют.
        bool exitDuringLoad = false;
        std::string outputPath;
        int warmupFrames = 300;
        int idleFrames = 300;
        int tailFrames = 300;
        double loadTimeoutMs = 60000.0;
    };

    explicit Benchmark(Config config);

    // Сцена редактора загружена — с этого кадра идёт прогрев.
    void onSceneReady();

    // Раз в кадр, в самом начале: сырое время прошлого кадра, до ограничения dt в Application.
    void beginFrame(double previousFrameMs);

    bool isFinished() const { return phase_ == Phase::Finished; }

private:
    enum class Phase { WaitingForScene, Warmup, Idle, Load, Tail, Finished };

    struct Sample {
        Phase phase;
        double frameMs;
        double mainLoadMs;
    };

    static const char* phaseName(Phase phase);
    void enter(Phase phase);
    void finish(bool complete);
    bool writeCsv(bool complete) const;

    Config config_;
    Phase phase_ = Phase::WaitingForScene;
    int phaseFrames_ = 0;
    LoadScenario scenario_;
    std::vector<Sample> samples_;

    // Кадр, который идёт сейчас: его длительность станет известна только в следующем beginFrame.
    Phase currentPhase_ = Phase::WaitingForScene;
    bool currentRecorded_ = false;
};
