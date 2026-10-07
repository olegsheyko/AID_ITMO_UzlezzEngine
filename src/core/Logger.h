#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <deque>
#include <mutex>
#include <vector>

#define LOG_INFO(msg) Logger::getInstance().log(Logger::Level::INFO, msg)
#define LOG_WARN(msg) Logger::getInstance().log(Logger::Level::WARN, msg)
#define LOG_ERROR(msg) Logger::getInstance().log(Logger::Level::ERROR, msg)

class Logger
{
public:
    enum class Level { INFO, WARN, ERROR };

    struct Entry {
        Level level = Level::INFO;
        std::string time;
        std::string message;
        std::uint64_t sequence = 0;
    };
    
    static Logger& getInstance()
    {
        static Logger instance;
        return instance;
    }
    
    void log(Level level, const std::string msg)
    {
        std::string prefix;
        switch (level)
        {
            case Level::INFO: prefix = "[INFO]"; break;
            case Level::WARN: prefix = "[WARN]"; break;
            case Level::ERROR: prefix = "[ERROR]"; break;
        }
        // Пишут и главный поток, и воркеры job system; localtime тоже не потокобезопасен.
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string time = getTimeStamp();
        std::string entry = time + " " + prefix + " " + msg;
        std::cout << entry << "\n";
        if (logFile_.is_open())
            logFile_ << entry << "\n";
        history_.push_back(Entry{level, time, msg, nextSequence_++});
        if (history_.size() > kHistoryLimit)
            history_.pop_front();
    }

    // Записи начиная с номера cursor (что успело вытесниться — пропадает); cursor сдвигается за последнюю.
    // Консоль редактора забирает так только новые строки, не копируя всю историю каждый кадр.
    void fetchSince(std::uint64_t& cursor, std::vector<Entry>& out)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!history_.empty() && cursor < nextSequence_)
        {
            // Номера идут подряд, так что начало находится без перебора.
            const std::uint64_t first = history_.front().sequence;
            const std::size_t start = cursor > first ? static_cast<std::size_t>(cursor - first) : 0;
            out.insert(out.end(), history_.begin() + static_cast<std::ptrdiff_t>(start), history_.end());
        }
        cursor = nextSequence_;
    }
    
    void openFile(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        logFile_.open(path, std::ios::app);
    }
private:
    Logger() = default;
    static constexpr std::size_t kHistoryLimit = 5000;
    std::mutex mutex_;
    std::ofstream logFile_;
    std::deque<Entry> history_;
    std::uint64_t nextSequence_ = 0;
    std::string getTimeStamp()
    {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        char buf[20];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
        return std::string(buf);
    }
};