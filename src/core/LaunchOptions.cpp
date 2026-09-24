#include "core/LaunchOptions.h"

#include <cstdlib>

bool parseLaunchOptions(int argc, char** argv, LaunchOptions& outOptions, std::string& outError) {
    outOptions = LaunchOptions{};
    std::string benchOut;
    std::string loadMode;
    bool exitDuringLoad = false;
    std::string stressOut;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--animation-bench") {
            if (i+1>=argc) { outError="--animation-bench expects sequential or parallel"; return false; }
            const std::string mode=argv[++i];
            if (mode!="sequential" && mode!="parallel") { outError="Invalid animation mode"; return false; }
            outOptions.animationBench=AnimationBenchmark::Config{};
            outOptions.animationBench->parallel=mode=="parallel";
        } else if (arg == "--animation-characters" || arg == "--animation-frames" || arg == "--animation-out") {
            if (!outOptions.animationBench || i+1>=argc) { outError=arg+" requires preceding --animation-bench and a value"; return false; }
            const char* value=argv[++i];
            if (arg=="--animation-out") outOptions.animationBench->output=value;
            else {
                char* end=nullptr; const long n=std::strtol(value,&end,10);
                if (*end || n<1 || n>(arg=="--animation-characters" ? 2048 : 10000000)) { outError="Invalid animation count"; return false; }
                if (arg=="--animation-characters") outOptions.animationBench->characters=static_cast<unsigned int>(n);
                else outOptions.animationBench->frames=static_cast<unsigned int>(n);
            }
        } else if (arg=="--animation-wait-tracy" || arg=="--animation-exit-loading") {
            if (!outOptions.animationBench) { outError=arg+" requires preceding --animation-bench"; return false; }
            if (arg=="--animation-wait-tracy") outOptions.animationBench->waitTracy=true;
            else outOptions.animationBench->exitLoading=true;
        } else if (arg == "--no-vsync") {
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
        } else if (arg == "--upload-budget-ms") {
            if (i + 1 >= argc) {
                outError = "--upload-budget-ms expects a number of milliseconds";
                return false;
            }
            char* end = nullptr;
            outOptions.uploadBudgetMs = std::strtod(argv[++i], &end);
            if (end == argv[i] || *end != '\0' || outOptions.uploadBudgetMs < 0.0) {
                outError = std::string("invalid upload budget: ") + argv[i];
                return false;
            }
        } else if (arg == "--stress-seconds") {
            if (i + 1 >= argc) {
                outError = "--stress-seconds expects a duration in seconds";
                return false;
            }
            char* end = nullptr;
            StressConfig config;
            config.durationSeconds = std::strtod(argv[++i], &end);
            if (end == argv[i] || *end != '\0' || config.durationSeconds <= 0.0) {
                outError = std::string("invalid stress duration: ") + argv[i];
                return false;
            }
            outOptions.stress = config;
        } else if (arg == "--stress-out") {
            if (i + 1 >= argc) {
                outError = "--stress-out expects a file path";
                return false;
            }
            stressOut = argv[++i];
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
    if (outOptions.bench && outOptions.stress) {
        outError = "--bench and --stress-seconds cannot be combined";
        return false;
    }
    if (outOptions.animationBench && outOptions.bench) { outError="Animation and loading benchmarks are separate modes"; return false; }
    if (outOptions.animationBench) outOptions.vsync=false;
    if (!stressOut.empty() && !outOptions.stress) {
        outError = "--stress-out requires --stress-seconds";
        return false;
    }
    if (outOptions.stress) {
        outOptions.stress->summaryPath = stressOut;
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
    return "Usage: GameEngine [--no-vsync] [--bench burst|stream [--load-mode async|sync] [--exit-during-load] [--bench-out file.csv]] [--stress-seconds s [--stress-out file]] [--upload-budget-ms ms] [--animation-bench sequential|parallel [--animation-characters N] [--animation-frames N] [--animation-out file.csv] [--animation-wait-tracy] [--animation-exit-loading]]";
}
