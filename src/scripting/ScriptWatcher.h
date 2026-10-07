#pragma once
#include "jobs/JobSystem.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Вотчер .lua для hot reload. Тяжёлая часть — обход папок, сравнение меток, чтение файла и проверка
// синтаксиса в одноразовом lua_State — идёт задачей job system на воркере. Главный поток только
// ставит задачу и забирает готовые изменения; применяет их ScriptSystem там же, на главном потоке.
class ScriptWatcher {
public:
    struct Change {
        std::string path;   // как в ScriptComponent: "assets/scripts/enemy.lua"
        std::string source; // прочитанный текст — reload не перечитывает файл, гонки с редактором нет
        bool ok = false;    // синтаксис верен
        std::string error;  // "assets/scripts/enemy.lua:12: ..." при ошибке
    };

    ScriptWatcher() = default;
    ~ScriptWatcher();
    ScriptWatcher(const ScriptWatcher&) = delete;
    ScriptWatcher& operator=(const ScriptWatcher&) = delete;

    // Главный поток, каждый кадр. Раз в interval ставит задачу проверки, если прошлая уже закончилась.
    // roots — папки, где ищутся все *.lua; paths — файлы, используемые сценой (могут лежать и вне roots).
    void poll(double nowSeconds, const std::vector<std::string>& roots, const std::vector<std::string>& paths);
    // Пора ли ставить новую задачу: интервал вышел и прошлая задача закончилась.
    bool due(double nowSeconds) const { return nowSeconds - lastPoll_ >= interval && (!job_.isValid() || job_.isDone()); }
    // Готовые изменения. Файл отдаётся после того, как его метка не менялась два опроса подряд:
    // редактор, пишущий файл по частям, не даст ложной синтаксической ошибки.
    std::vector<Change> drain();
    // Синхронный проход в вызывающем потоке — для тестов и для работы без job system.
    void scanNow(const std::vector<std::string>& roots, const std::vector<std::string>& paths);

    double interval = 0.2; // секунды между опросами
    std::size_t scans() const;

    // Проверка синтаксиса без выполнения: компиляция в отдельном lua_State.
    static bool validate(const std::string& path, const std::string& source, std::string& error);

private:
    struct Stamp {
        std::filesystem::file_time_type time{};
        std::uintmax_t size = 0;
        bool operator==(const Stamp& other) const { return time == other.time && size == other.size; }
        bool operator!=(const Stamp& other) const { return !(*this == other); }
    };
    struct Shared {
        std::mutex mutex;
        std::map<std::string, Stamp> known;   // последняя применённая версия
        std::map<std::string, Stamp> pending; // изменилась, ждём стабильности
        std::vector<Change> ready;
        std::size_t scans = 0;
    };
    static void scan(Shared& shared, const std::vector<std::string>& roots, const std::vector<std::string>& paths);

    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
    JobHandle job_;
    double lastPoll_ = -1.0e9;
};
