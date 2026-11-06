// Публичный интерфейс скриптовой подсистемы (ЛР 2).
// Здесь описано, что редактор может делать со скриптами: загрузить, запустить Play, обновлять каждый кадр,
// горячо подменить код и остановить. Вся реализация (виртуальная машина Lua, биндинги, песочница)
// скрыта в ScriptSystem.cpp за указателем Impl, чтобы заголовок не тянул sol2 и Lua в остальной движок.
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

// C++ база всех скриптовых поведений. Три события жизни объекта:
//   on_create  один раз при создании экземпляра (старт Play или спавн сущности);
//   on_update  каждый кадр Play, dt в секундах;
//   on_destroy при уничтожении сущности и при Stop.
// Методы виртуальные и по умолчанию пустые, поэтому Lua-класс переопределяет только нужные.
// Сам вызов в Lua делает адаптер LuaBehaviour (ScriptSystem.cpp), он же играет роль trampoline из лекции.
class ScriptBehaviour {
public:
    virtual ~ScriptBehaviour() = default;
    virtual void on_create() {}
    virtual void on_update(float) {}
    virtual void on_destroy() {}
};

// Owns one Lua state: created with the system, closed after all Lua references.
// Components/snapshots are plain data. All Lua calls run on the main thread.
//
// Жизненный цикл, как его видит редактор:
//   Edit:  reload() читает .lua файлы сцены и проверяет классы, hotReload() подменяет один файл;
//   Play:  start() = reload() + создание экземпляров для всех ScriptComponent;
//          update() каждый кадр зовёт on_update у каждого экземпляра;
//   Stop:  stop() вызывает on_destroy и освобождает экземпляры, сама виртуальная машина живёт дальше.
class ScriptSystem {
public:
    explicit ScriptSystem(World& world);
    ~ScriptSystem();
    // Загрузить классы всех скриптов, которые нужны компонентам сцены, и заполнить поля значениями по умолчанию.
    // Работает только вне Play. Транзакция: новые классы подменяют старые только если загрузились все.
    bool reload(); // Edit mode only. Transactional class validation.
    // Перезагрузка одного файла из уже прочитанного текста, в Edit и в Play. Транзакционно:
    // при ошибке компиляции или проверки класса работает прежний код.
    HotReloadReport hotReload(const std::string& path, const std::string& source, ReloadMode mode = ReloadMode::KeepState);
    // Файлы скриптов, нужные сцене и уже загруженные (для вотчера).
    std::vector<std::string> scriptPaths() const;
    // Войти в Play: перезагрузить классы и создать экземпляр (вызвать on_create) для каждой сущности со скриптом.
    // При любой ошибке откатывается назад и возвращает false, Play не начинается.
    bool start();
    // Выйти из Play: on_destroy у всех экземпляров, сброс API-состояния, сборка мусора Lua.
    void stop();
    // Один кадр: on_update каждому живому экземпляру, затем применяются отложенные destroy.
    // allowInput: ловит ли игровое окно клавиатуру (иначе input_pressed в скрипте всегда false).
    void update(float dt, bool allowInput);
    // Подставить в ScriptComponent значения полей по умолчанию из Lua-класса (нужно для Inspector в Edit).
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
    // Внешние услуги, которые редактор подставляет снаружи. Так скриптовая подсистема не знает
    // ни про PrefabManager, ни про InputManager, и тесты могут подставить свои заглушки.
    // Injected generic prefab service; tests can use a CPU-only implementation.
    std::function<Entity(const std::string&)> spawnPrefab;  // принимает путь префаба, возвращает новую сущность
    std::function<bool(const std::string&)> inputPressed;   // принимает имя действия, говорит нажато ли оно в этом кадре
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
