#pragma once
#include "ecs/World.h"
#include "scripting/ScriptComponent.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Лимиты скрипта: бюджет времени на один entry point и потолок кучи Lua.
struct ScriptLimits {
    int timeBudgetMs = 100;
    std::size_t memoryBytes = std::size_t(256) << 20;
};

struct ScriptStats {
    std::size_t memoryBytes = 0;
    std::size_t peakMemoryBytes = 0;
    std::size_t instances = 0;
    std::size_t failedInstances = 0;
    double updateMs = 0.0; // последний ScriptSystem::update
};

// Что делать с живыми экземплярами при hot reload в Play.
enum class ReloadMode {
    KeepState, // L2: новый код, self.* переносится (кроме полей, чьё начальное значение поменяли в коде)
    Reset,     // L1: новый код, состояние экземпляра заново из on_create
};

struct HotReloadReport {
    bool ok = false;
    std::size_t classes = 0;         // заменённых классов
    std::size_t instances = 0;       // экземпляров на новом коде
    std::size_t keptFields = 0;      // перенесённых полей self.* (L2)
    std::size_t migrated = 0;        // экземпляров, перенесённых своим on_reload(old)
    std::size_t revived = 0;         // экземпляров, отключённых ошибкой и оживших с новым кодом
    std::size_t failedInstances = 0; // новый on_create упал — экземпляр остался на старом коде
    std::string error;
};

class ScriptBehaviour {
public:
    virtual ~ScriptBehaviour() = default;
    virtual void on_create() {}
    virtual void on_update(float) {}
    virtual void on_destroy() {}
};

// Owns one Lua state: created with the system, closed after all Lua references.
// Components/snapshots are plain data. All Lua calls run on the main thread.
class ScriptSystem {
public:
    explicit ScriptSystem(World& world);
    ~ScriptSystem();
    bool reload(); // Edit mode only. Transactional class validation.
    // Перезагрузка одного файла из уже прочитанного текста, в Edit и в Play. Транзакционно:
    // при ошибке компиляции или проверки класса работает прежний код.
    HotReloadReport hotReload(const std::string& path, const std::string& source, ReloadMode mode = ReloadMode::KeepState);
    // Файлы скриптов, нужные сцене и уже загруженные (для вотчера).
    std::vector<std::string> scriptPaths() const;
    bool start();
    void stop();
    void update(float dt, bool allowInput);
    bool attachDefaults(Entity entity);
    bool running() const;
    const std::string& error() const;
    void clearError();
    const std::string& status() const;
    size_t instanceCount() const;
    ScriptStats stats() const;
    void setLimits(const ScriptLimits& limits);
    ScriptLimits limits() const;
    // "Entity:get_position", "World:spawn_prefab"… — что реально привязано; сверяется со stubs для IDE.
    std::vector<std::string> apiNames() const;
    // Injected generic prefab service; tests can use a CPU-only implementation.
    std::function<Entity(const std::string&)> spawnPrefab;
    std::function<bool(const std::string&)> inputPressed;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
