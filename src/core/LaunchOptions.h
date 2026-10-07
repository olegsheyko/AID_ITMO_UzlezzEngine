#pragma once

#include "bench/Benchmark.h"
#include "bench/StressRun.h"
#include "bench/AnimationBenchmark.h"

#include <optional>
#include <string>
#include <vector>

// Запуск редактора для автоматических скриншотов: какое окно открыть, что выбрать, куда сохранить кадр.
struct EditorStartupOptions {
    std::string screenshotPath;
    std::string scriptPath;
    // Файл раскладки для скриншотного режима (по умолчанию там раскладка не сохраняется).
    std::string iniPath;
    int screenshotFrames = 120;
    int windowWidth = 1600;
    int windowHeight = 1000;
    std::string selectEntity;
    std::string selectAsset;
    std::string browseFolder;
    std::vector<std::string> focusWindows;
    bool play = false;
    bool showColliders = false;
    bool listView = false;
    bool resetLayout = false;
};

struct LaunchOptions {
    bool vsync = true;
    // Бюджет пампа заливки на кадр, мс; отрицательное — оставить значение по умолчанию.
    double uploadBudgetMs = -1.0;
    std::optional<Benchmark::Config> bench;
    std::optional<StressConfig> stress;
    std::optional<AnimationBenchmark::Config> animationBench;
    EditorStartupOptions editor;
};

// Аргументы запуска: --no-vsync, --bench burst|stream, --bench-out <файл.csv>, --load-mode async|sync,
// --exit-during-load, --upload-budget-ms <мс>, --stress-seconds <с>, --stress-out <файл>.
// Скриншот редактора: --editor-screenshot <файл.png> [--editor-frames N] [--window-size WxH]
// [--editor-select <имя>] [--editor-asset <путь>] [--editor-browse <папка>] [--editor-tab <окно>]... [--editor-play].
// Режим --bench всегда выключает vsync — иначе время кадра прилипает к частоте экрана.
bool parseLaunchOptions(int argc, char** argv, LaunchOptions& outOptions, std::string& outError);

const char* launchUsage();
