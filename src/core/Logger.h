#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <ctime>
#include <mutex>

#define LOG_INFO(msg) Logger::getInstance().log(Logger::Level::INFO, msg)
#define LOG_WARN(msg) Logger::getInstance().log(Logger::Level::WARN, msg)
#define LOG_ERROR(msg) Logger::getInstance().log(Logger::Level::ERROR, msg)

class Logger
{
public:
    enum class Level { INFO, WARN, ERROR };
    
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
        std::string entry = getTimeStamp()+ " " + prefix + " " + msg;
        std::cout << entry << "\n";
        if (logFile_.is_open())
            logFile_ << entry << "\n";
    }
    
    void openFile(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        logFile_.open(path, std::ios::app);
    }
private:
    Logger() = default;
    std::mutex mutex_;
    std::ofstream logFile_;
    std::string getTimeStamp()
    {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        char buf[20];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
        return std::string(buf);
    }
};