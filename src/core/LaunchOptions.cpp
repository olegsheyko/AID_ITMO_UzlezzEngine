#include "core/LaunchOptions.h"

bool parseLaunchOptions(int argc, char** argv, LaunchOptions& outOptions, std::string& outError) {
    outOptions = LaunchOptions{};
    std::string benchOut;
    std::string loadMode;
    bool exitDuringLoad = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--no-vsync") {
            outOptions.vsync = false;
        } else if (arg == "--bench") {
            if (i + 1 >= argc) {
                outError = "--bench expects a scenario: burst or stream";
                return false;
            }
            Benchmark::Config config;
            if (!LoadScenario::parseMode(argv[++i], config.mode)) {
                outError = std::string("unknown bench scenario: ") + argv[i];
                return false;
            }
            outOptions.bench = config;
        } else if (arg == "--load-mode") {
            if (i + 1 >= argc) {
                outError = "--load-mode expects async or sync";
                return false;
            }
            loadMode = argv[++i];
            if (loadMode != "async" && loadMode != "sync") {
                outError = "unknown load mode: " + loadMode;
                return false;
            }
        } else if (arg == "--exit-during-load") {
            exitDuringLoad = true;
        } else if (arg == "--bench-out") {
            if (i + 1 >= argc) {
                outError = "--bench-out expects a file path";
                return false;
            }
            benchOut = argv[++i];
        } else {
            outError = "unknown argument: " + arg;
            return false;
        }
    }

    if ((!benchOut.empty() || !loadMode.empty() || exitDuringLoad) && !outOptions.bench) {
        outError = "--bench-out, --load-mode and --exit-during-load require --bench";
        return false;
    }
    if (outOptions.bench) {
        outOptions.vsync = false;
        outOptions.bench->asyncLoading = loadMode != "sync";
        outOptions.bench->exitDuringLoad = exitDuringLoad;
        outOptions.bench->outputPath = benchOut.empty()
            ? std::string("bench/") + LoadScenario::modeName(outOptions.bench->mode) + ".csv"
            : benchOut;
    }
    return true;
}

const char* launchUsage() {
    return "Usage: GameEngine [--no-vsync] [--bench burst|stream [--load-mode async|sync] [--exit-during-load] [--bench-out file.csv]]";
}
