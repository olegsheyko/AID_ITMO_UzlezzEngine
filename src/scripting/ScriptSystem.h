#pragma once
#include "ecs/World.h"
#include "scripting/ScriptComponent.h"
#include <functional>
#include <memory>
#include <string>

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
    bool start();
    void stop();
    void update(float dt, bool allowInput);
    void attachDefaults(Entity entity);
    bool running() const;
    const std::string& error() const;
    const std::string& status() const;
    size_t instanceCount() const;
    // Injected generic prefab service; tests can use a CPU-only implementation.
    std::function<Entity(const std::string&)> spawnPrefab;
    std::function<bool(const std::string&)> inputPressed;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
