#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <ctime>

// Система горячей замены ресурсов
class HotReload {
public:
    static HotReload& getInstance();

    // Регистрация файла для отслеживания
    void watchFile(const std::string& path);

    // Проверка изменений (вызывать каждый кадр)
    // Возвращает true если были изменения
    bool update();

    // Получить список изменённых файлов
    const std::vector<std::string>& getChangedFiles() const { return changedFiles_; }

private:
    HotReload() = default;
    ~HotReload() = default;
    HotReload(const HotReload&) = delete;
    HotReload& operator=(const HotReload&) = delete;

    struct FileInfo {
        std::time_t lastWriteTime;
    };

    std::time_t getFileModTime(const std::string& path);

    std::unordered_map<std::string, FileInfo> watchedFiles_;
    std::vector<std::string> changedFiles_;
};
