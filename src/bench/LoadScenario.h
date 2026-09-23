#pragma once

#include "resources/Resource.h"
#include "resources/ResourceTypes.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Тяжёлая пачка текстур для замеров ЛР 1 — все картинки из assets/models.
// Burst запрашивает всю пачку в одном кадре. Stream — по одной текстуре
// по расписанию раз в kStreamInterval, но не больше одной за кадр.
// Async грузит через job system, sync — прежним путём на главном потоке, для сравнения.
class LoadScenario {
public:
    enum class Mode { Burst, Stream };

    static const char* modeName(Mode mode);
    static bool parseMode(const std::string& text, Mode& outMode);

    // Перед стартом отпускает прошлую пачку, чтобы повторный прогон грузил с диска, а не из кэша.
    void start(Mode mode, bool async);

    // Раз в кадр на главном потоке: ставит запросы по расписанию и отмечает готовность пачки.
    void update();

    bool isRunning() const { return running_; }
    bool isComplete() const { return started_ && !running_; }
    Mode mode() const { return mode_; }
    bool isAsync() const { return async_; }
    // Текстуры, которые уже лежали в кэше и не грузились заново — например, их держит выбор текстур.
    std::size_t fromCacheCount() const { return fromCache_; }
    std::size_t requestedCount() const { return nextIndex_; }
    std::size_t totalCount() const { return paths_.size(); }
    std::size_t failedCount() const { return failed_; }
    std::uintmax_t batchBytes() const { return batchBytes_; }
    // От start() до готовности всей пачки; пока пачка грузится — время с начала.
    double elapsedMs() const;

private:
    using Clock = std::chrono::steady_clock;

    void request(std::size_t index);
    bool allReady() const;

    Mode mode_ = Mode::Burst;
    bool async_ = true;
    bool started_ = false;
    bool running_ = false;
    std::vector<std::string> paths_;
    std::vector<std::shared_ptr<Resource<TextureData>>> handles_;
    std::size_t nextIndex_ = 0;
    std::size_t failed_ = 0;
    std::size_t fromCache_ = 0;
    std::uintmax_t batchBytes_ = 0;
    Clock::time_point startTime_{};
    double batchMs_ = 0.0;
};
