#pragma once

#include "ecs/Component.h"
#include "ecs/Entity.h"
#include "ecs/System.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class World {
public:
    Entity createEntity();
    Entity createEntityWithId(Entity entity);
    void destroyEntity(Entity entity);
    void clear();

    bool isAlive(Entity entity) const;
    std::vector<Entity> getEntities() const;
    std::size_t getEntityCount() const { return aliveEntities_.size(); }

    void addUpdateSystem(UpdateSystem& system);
    void addRenderSystem(RenderSystemBase& system);
    void updateSystems(float dt);
    void renderSystems();

    template <typename T, typename... Args>
    T& addComponent(Entity entity, Args&&... args) {
        static_assert(kIsComponent<T>, "T must satisfy the Component contract");
        assert(isAlive(entity));
        auto& storage = getOrCreateStorage<T>().components;
        auto [it, inserted] = storage.emplace(entity, T{std::forward<Args>(args)...});
        if (!inserted) {
            it->second = T{std::forward<Args>(args)...};
        }
        return it->second;
    }

    template <typename T>
    bool hasComponent(Entity entity) const {
        static_assert(kIsComponent<T>, "T must satisfy the Component contract");
        const auto* storage = tryGetStorage<T>();
        return storage != nullptr && storage->components.find(entity) != storage->components.end();
    }

    template <typename T>
    T& getComponent(Entity entity) {
        static_assert(kIsComponent<T>, "T must satisfy the Component contract");
        assert(isAlive(entity));
        return getStorage<T>().components.at(entity);
    }

    template <typename T>
    const T& getComponent(Entity entity) const {
        static_assert(kIsComponent<T>, "T must satisfy the Component contract");
        assert(isAlive(entity));
        return getStorage<T>().components.at(entity);
    }

    template <typename T>
    void removeComponent(Entity entity) {
        static_assert(kIsComponent<T>, "T must satisfy the Component contract");
        auto* storage = tryGetStorage<T>();
        if (storage != nullptr) {
            storage->components.erase(entity);
        }
    }

    template <typename TFirst, typename... TRest, typename Func>
    void forEach(Func&& func) {
        static_assert(kIsComponent<TFirst> && (... && kIsComponent<TRest>), "All component types must satisfy the Component contract");
        auto* firstStorage = tryGetStorage<TFirst>();
        if (firstStorage == nullptr) {
            return;
        }

        for (auto& [entity, firstComponent] : firstStorage->components) {
            if ((hasComponent<TRest>(entity) && ...)) {
                func(entity, firstComponent, getComponent<TRest>(entity)...);
            }
        }
    }

private:
    struct IStorage {
        virtual ~IStorage() = default;
        virtual void remove(Entity entity) = 0;
    };

    template <typename T>
    struct Storage final : IStorage {
        std::unordered_map<Entity, T> components;

        void remove(Entity entity) override {
            components.erase(entity);
        }
    };

    template <typename T>
    Storage<T>& getOrCreateStorage() {
        const std::type_index type = std::type_index(typeid(T));
        auto it = storages_.find(type);
        if (it == storages_.end()) {
            auto storage = std::make_unique<Storage<T>>();
            auto* raw = storage.get();
            storages_.emplace(type, std::move(storage));
            return *raw;
        }

        return *static_cast<Storage<T>*>(it->second.get());
    }

    template <typename T>
    Storage<T>& getStorage() {
        const std::type_index type = std::type_index(typeid(T));
        return *static_cast<Storage<T>*>(storages_.at(type).get());
    }

    template <typename T>
    const Storage<T>& getStorage() const {
        const std::type_index type = std::type_index(typeid(T));
        return *static_cast<const Storage<T>*>(storages_.at(type).get());
    }

    template <typename T>
    const Storage<T>* tryGetStorage() const {
        const std::type_index type = std::type_index(typeid(T));
        auto it = storages_.find(type);
        if (it == storages_.end()) {
            return nullptr;
        }

        return static_cast<const Storage<T>*>(it->second.get());
    }

    template <typename T>
    Storage<T>* tryGetStorage() {
        const std::type_index type = std::type_index(typeid(T));
        auto it = storages_.find(type);
        if (it == storages_.end()) {
            return nullptr;
        }

        return static_cast<Storage<T>*>(it->second.get());
    }

    Entity nextEntity_ = 1;
    std::unordered_set<Entity> aliveEntities_;
    std::unordered_map<std::type_index, std::unique_ptr<IStorage>> storages_;
    std::vector<UpdateSystem*> updateSystems_;
    std::vector<RenderSystemBase*> renderSystems_;
};
