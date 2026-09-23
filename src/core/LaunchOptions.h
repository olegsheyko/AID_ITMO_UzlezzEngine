#pragma once

#include "bench/Benchmark.h"
#include "bench/StressRun.h"

#include <optional>
#include <string>

struct LaunchOptions {
    bool vsync = true;
    // Бюджет пампа заливки на кадр, мс; отрицательное — оставить значение по умолчанию.
    double uploadBudgetMs = -1.0;
    std::optional<Benchmark::Config> bench;
    std::optional<StressConfig> stress;
};

// Аргументы запуска: --no-vsync, --bench burst|stream, --bench-out <файл.csv>, --load-mode async|sync,
// --exit-during-load, --upload-budget-ms <мс>, --stress-seconds <с>, --stress-out <файл>.
// Режим --bench всегда выключает vsync — иначе время кадра прилипает к частоте экрана.
bool parseLaunchOptions(int argc, char** argv, LaunchOptions& outOptions, std::string& outError);

const char* launchUsage();
