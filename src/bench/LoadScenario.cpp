#include "bench/LoadScenario.h"

#include "core/Logger.h"
#include "resources/ResourceManager.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace {
constexpr const char* kBatchRoot = "assets/models";
constexpr std::chrono::milliseconds kStreamInterval{100};

bool isImageFile(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".dds";
}

double toMs(std::chrono::steady_clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}
}

const char* LoadScenario::modeName(Mode mode) {
    return mode == Mode::Burst ? "burst" : "stream";
}

bool LoadScenario::parseMode(const std::string& text, Mode& outMode) {
    if (text == "burst") {
        outMode = Mode::Burst;
        return true;
    }
    if (text == "stream") {
        outMode = Mode::Stream;
        return true;
    }
    return false;
}

void LoadScenario::start(Mode mode, bool async) {
    // Сначала отпустить свои хэндлы, потом выгрузить: releaseTexture не трогает то, что ещё кто-то держит.
    const std::vector<std::string> previous = std::move(paths_);
    handles_.clear();
    for (const std::string& path : previous) {
        ResourceManager::getInstance().releaseTexture(path);
    }

    mode_ = mode;
    async_ = async;
    paths_.clear();
    nextIndex_ = 0;
    failed_ = 0;
    fromCache_ = 0;
    batchBytes_ = 0;
    batchMs_ = 0.0;

    // Отсортированный список — одинаковый порядок загрузки во всех прогонах.
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(kBatchRoot, error), end; !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error) && isImageFile(it->path())) {
            paths_.push_back(it->path().generic_string());
            batchBytes_ += it->file_size(error);
        }
    }
    std::sort(paths_.begin(), paths_.end());
    handles_.resize(paths_.size());

    started_ = true;
    running_ = !paths_.empty();
    startTime_ = Clock::now();
    LOG_INFO("LoadScenario: " + std::string(modeName(mode)) + (async ? " async" : " sync") + ", " +
        std::to_string(paths_.size()) + " textures, " + std::to_string(batchBytes_ / 1024) + " KB on disk");
    if (paths_.empty()) {
        LOG_ERROR("LoadScenario: no images found in " + std::string(kBatchRoot));
    }
}

void LoadScenario::update() {
    if (!running_) {
        return;
    }

    const Clock::time_point frameStart = Clock::now();
    if (mode_ == Mode::Burst) {
        ZoneScopedN("Heavy batch: burst");
        while (nextIndex_ < paths_.size()) {
            request(nextIndex_++);
        }
    } else if (nextIndex_ < paths_.size() && frameStart >= startTime_ + kStreamInterval * static_cast<int>(nextIndex_)) {
        ZoneScopedN("Heavy batch: stream");
        request(nextIndex_++);
    }
    if (nextIndex_ == paths_.size() && allReady()) {
        running_ = false;
        batchMs_ = toMs(Clock::now() - startTime_);
        failed_ = static_cast<std::size_t>(std::count_if(handles_.begin(), handles_.end(),
            [](const auto& handle) { return !handle || handle->isFailed(); }));
        TracyMessageL("Heavy batch complete");
        LOG_INFO("LoadScenario: batch ready in " + std::to_string(batchMs_) + " ms, failed " + std::to_string(failed_) +
            ", from cache " + std::to_string(fromCache_));
    }
}

double LoadScenario::elapsedMs() const {
    if (!started_) {
        return 0.0;
    }
    return running_ ? toMs(Clock::now() - startTime_) : batchMs_;
}

void LoadScenario::request(std::size_t index) {
    ResourceManager& resources = ResourceManager::getInstance();
    handles_[index] = async_
        ? resources.loadTextureAsync(paths_[index], JobPriority::Normal)
        : resources.load<TextureData>(paths_[index]);
    // Асинхронный хэндл готов сразу, только если текстура уже была в кэше.
    if (async_ && handles_[index] && handles_[index]->isLoaded()) {
        ++fromCache_;
    }
}

bool LoadScenario::allReady() const {
    return std::all_of(handles_.begin(), handles_.end(), [](const auto& handle) {
        // nullptr — загрузка не удалась, ждать нечего; Failed тоже финал.
        return !handle || !handle->isPending();
    });
}
