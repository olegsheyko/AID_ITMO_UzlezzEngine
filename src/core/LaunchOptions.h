#pragma once

#include "bench/Benchmark.h"

#include <optional>
#include <string>

struct LaunchOptions {
    bool vsync = true;
    std::optional<Benchmark::Config> bench;
};

// Аргументы запуска: --no-vsync, --bench burst|stream, --bench-out <файл.csv>.
// Режим --bench всегда выключает vsync — иначе время кадра прилипает к частоте экрана.
bool parseLaunchOptions(int argc, char** argv, LaunchOptions& outOptions, std::string& outError);

const char* launchUsage();
