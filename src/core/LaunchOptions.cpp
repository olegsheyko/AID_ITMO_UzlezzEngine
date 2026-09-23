#include "core/LaunchOptions.h"

bool parseLaunchOptions(int argc, char** argv, LaunchOptions& outOptions, std::string& outError) {
    outOptions = LaunchOptions{};
    std::string benchOut;

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

    if (!benchOut.empty() && !outOptions.bench) {
        outError = "--bench-out requires --bench";
        return false;
    }
    if (outOptions.bench) {
        outOptions.vsync = false;
        outOptions.bench->outputPath = benchOut.empty()
            ? std::string("bench/") + LoadScenario::modeName(outOptions.bench->mode) + ".csv"
            : benchOut;
    }
    return true;
}

const char* launchUsage() {
    return "Usage: GameEngine [--no-vsync] [--bench burst|stream [--bench-out file.csv]]";
}
