#include "scripting/ScriptSystem.h"
#include "ecs/Components.h"
#include "core/Logger.h"
#include <sol/sol.hpp>
#include <cmath>
#include <limits>
#include <set>

namespace {
struct EntityHandle {
    World* world = nullptr;
    std::weak_ptr<int> lifetime;
    Entity id = kInvalidEntity;
    std::uint64_t generation = 0;
    bool valid() const {
        return !lifetime.expired() && world && world->isAlive(id) && world->generation(id) == generation;
    }
    void check() const { if (!valid()) throw std::runtime_error("Stale EntityHandle"); }
};
EntityHandle handle(World& w, Entity e) { return {&w, w.lifetime(), e, w.generation(e)}; }
struct ApiState {
    World* world = nullptr;
    bool active = false, allowInput = false;
    std::function<Entity(const std::string&)> spawn;
    std::function<bool(const std::string&)> pressed;
    std::set<Entity> destroyed;
    std::string status;
};
struct WorldApi {
    std::weak_ptr<ApiState> state;
    std::shared_ptr<ApiState> get() const {
        auto s = state.lock();
        if (!s || !s->active) throw std::runtime_error("Script world is no longer active");
        return s;
    }
};
ScriptValue fromLua(const sol::object& value) {
    switch (value.get_type()) {
    case sol::type::boolean: return value.as<bool>();
    case sol::type::string: return value.as<std::string>();
    case sol::type::number: {
        lua_State* state = value.lua_state();
        value.push();
        const bool integer = lua_isinteger(state, -1) != 0;
        const auto i = lua_tointeger(state, -1);
        const double number = lua_tonumber(state, -1);
        lua_pop(state, 1);
        if (integer) {
            if (i < std::numeric_limits<int>::min() || i > std::numeric_limits<int>::max())
                throw std::runtime_error("Script integer exceeds int32 range");
            return static_cast<int>(i);
        }
        if (!std::isfinite(number)) throw std::runtime_error("Script field must be finite");
        return number;
    }
    default: throw std::runtime_error("Script fields support bool, integer, number and string only");
    }
}
sol::object toLua(sol::this_state state, const ScriptValue& value) {
    return std::visit([&](const auto& v) { return sol::make_object(state, v); }, value);
}
void checkPosition(Vec3 p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
        throw std::runtime_error("Position must be finite");
}
// C++ virtual adapter; Lua instances inherit prototypes from uzlezz.behaviour().
class LuaBehaviour final : public ScriptBehaviour {
public:
    LuaBehaviour(sol::table object, sol::table prototype)
        : object_(std::move(object)), prototype_(std::move(prototype)) {}
    void on_create() override { call("on_create"); }
    void on_update(float dt) override { call("on_update", dt); }
    void on_destroy() override { call("on_destroy"); }
private:
    template<class... Args> void call(const char* name, Args... args) {
        sol::object method = prototype_.raw_get<sol::object>(name);
        if (!method.valid() || method == sol::lua_nil) return;
        sol::protected_function function = method.as<sol::protected_function>();
        sol::protected_function_result result = function(object_, args...);
        if (!result.valid()) { sol::error error = result; throw error; }
    }
    sol::table object_, prototype_;
};
void bindEngine(sol::state& lua) {
    auto api = lua.create_named_table("uzlezz");
    api.new_usertype<ScriptBehaviour>("ScriptBehaviour", sol::no_constructor,
        "on_create", &ScriptBehaviour::on_create, "on_update", &ScriptBehaviour::on_update,
        "on_destroy", &ScriptBehaviour::on_destroy);
    api.new_usertype<Vec3>("Vec3", sol::call_constructor,
        sol::factories([](float x, float y, float z) { return Vec3{x,y,z}; }),
        "x", &Vec3::x, "y", &Vec3::y, "z", &Vec3::z);
    api.new_usertype<EntityHandle>("Entity", sol::no_constructor,
        "is_alive", &EntityHandle::valid,
        "id", sol::property([](const EntityHandle& h) { h.check(); return h.id; }),
        "get_position", [](const EntityHandle& h) { h.check(); return h.world->getComponent<Transform>(h.id).position; },
        "set_position", [](const EntityHandle& h, Vec3 p) { h.check(); checkPosition(p); h.world->getComponent<Transform>(h.id).position = p; },
        "set_yaw", [](const EntityHandle& h, float yaw) {
            h.check(); if (!std::isfinite(yaw)) throw std::runtime_error("Yaw must be finite");
            h.world->getComponent<Transform>(h.id).rotation.z = yaw;
        },
        "get_tag", [](const EntityHandle& h) { h.check(); return h.world->hasComponent<Tag>(h.id) ? h.world->getComponent<Tag>(h.id).name : std::string{}; },
        "set_animation_speed", [](const EntityHandle& h, float speed) {
            h.check(); if (!std::isfinite(speed)) throw std::runtime_error("Speed must be finite");
            if (h.world->hasComponent<Animator>(h.id)) h.world->getComponent<Animator>(h.id).speed = speed;
        },
        "get_field", [](const EntityHandle& h, const std::string& name, sol::this_state state) {
            h.check(); return toLua(state, h.world->getComponent<ScriptComponent>(h.id).fields.at(name));
        },
        "set_field", [](const EntityHandle& h, const std::string& name, sol::object value) {
            h.check(); auto& old = h.world->getComponent<ScriptComponent>(h.id).fields.at(name);
            auto next = fromLua(value);
            if (old.index() != next.index()) throw std::runtime_error("Field type mismatch: " + name);
            old = next;
        });
    api.new_usertype<WorldApi>("World", sol::no_constructor,
        "input_pressed", [](const WorldApi& a, const std::string& action) {
            auto s = a.get(); return s->allowInput && s->pressed && s->pressed(action);
        },
        "find_by_tag", [](const WorldApi& a, const std::string& tag) {
            auto s = a.get();
            for (Entity e : s->world->getEntities())
                if (!s->destroyed.count(e) && s->world->hasComponent<Tag>(e) && s->world->getComponent<Tag>(e).name == tag)
                    return handle(*s->world, e);
            return EntityHandle{};
        },
        "find_all_by_tag", [](const WorldApi& a, const std::string& tag) {
            auto s = a.get(); std::vector<EntityHandle> result;
            for (Entity e : s->world->getEntities())
                if (!s->destroyed.count(e) && s->world->hasComponent<Tag>(e) && s->world->getComponent<Tag>(e).name == tag)
                    result.push_back(handle(*s->world, e));
            return sol::as_table(std::move(result));
        },
        "spawn_prefab", [](const WorldApi& a, const std::string& path, Vec3 position) {
            auto s = a.get(); checkPosition(position);
            if (!s->spawn) throw std::runtime_error("Prefab service unavailable");
            Entity e = s->spawn(path); auto h = handle(*s->world, e); h.check();
            s->world->getComponent<Transform>(e).position = position; return h;
        },
        "destroy", [](const WorldApi& a, const EntityHandle& h) {
            auto s = a.get(); h.check();
            if (h.world != s->world) throw std::runtime_error("Entity belongs to another world");
            s->destroyed.insert(h.id);
        },
        "set_status", [](const WorldApi& a, const std::string& text) { a.get()->status = text; });
    lua.script(R"(
        function uzlezz.behaviour(prototype)
            prototype.base = uzlezz.ScriptBehaviour
            return setmetatable(prototype, {__index = uzlezz.ScriptBehaviour})
        end
    )");
}
} // namespace

struct ScriptSystem::Impl {
    // Declared first, destroyed last: no sol reference may outlive this state.
    sol::state lua;
    World& world;
    std::shared_ptr<ApiState> api = std::make_shared<ApiState>();
    std::map<std::string, sol::table> classes;
    struct Instance {
        EntityHandle entity;
        std::unique_ptr<ScriptBehaviour> behaviour;
        bool failed = false;
    };
    std::map<Entity, Instance> instances;
    std::string error;
    explicit Impl(World& w) : world(w) {
        api->world = &w;
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);
        bindEngine(lua);
        LOG_INFO("Lua runtime initialized: " LUA_RELEASE " (sol2, statically linked)");
    }
    void report(const std::exception& e) { error = e.what(); LOG_ERROR("Lua: " + error); }
    std::string key(const ScriptComponent& c) const { return c.path + ":" + c.className; }
    sol::table load(const ScriptComponent& c) {
        sol::environment environment(lua, sol::create, lua.globals());
        auto result = lua.safe_script_file(c.path, environment, sol::script_pass_on_error);
        if (!result.valid()) { sol::error error = result; throw error; }
        sol::object object = environment[c.className];
        if (!object.is<sol::table>()) throw std::runtime_error(c.path + ": missing behaviour class " + c.className);
        auto cls = object.as<sol::table>();
        sol::object base = lua["uzlezz"]["ScriptBehaviour"];
        if (cls.raw_get<sol::object>("base") != base)
            throw std::runtime_error(c.path + ": class must extend uzlezz.ScriptBehaviour using uzlezz.behaviour");
        sol::object fields = cls.raw_get<sol::object>("fields");
        if (!fields.is<sol::table>()) throw std::runtime_error(c.path + ": missing fields table");
        for (const auto& item : fields.as<sol::table>()) {
            if (item.first.get_type() != sol::type::string) throw std::runtime_error(c.path + ": field name must be a string");
            fromLua(item.second);
        }
        for (const char* name : {"on_create", "on_update", "on_destroy"}) {
            sol::object method = cls.raw_get<sol::object>(name);
            if (method != sol::lua_nil && method.get_type() != sol::type::function)
                throw std::runtime_error(c.path + ": callback must be a function: " + name);
        }
        return cls;
    }
    void defaults(ScriptComponent& c, const sol::table& cls) {
        sol::table defaults = cls["fields"];
        for (const auto& item : defaults) {
            const auto name = item.first.as<std::string>();
            const auto value = fromLua(item.second);
            auto it = c.fields.find(name);
            if (it == c.fields.end()) c.fields[name] = value;
            else if (it->second.index() != value.index()) throw std::runtime_error(c.path + ": incompatible field type: " + name);
        }
        for (const auto& item : c.fields)
            if (defaults.raw_get<sol::object>(item.first) == sol::lua_nil) throw std::runtime_error(c.path + ": unknown field: " + item.first);
    }
    void create(Entity e) {
        auto& c = world.getComponent<ScriptComponent>(e);
        const auto k = key(c);
        if (!classes.count(k)) classes[k] = load(c);
        defaults(c, classes.at(k));
        sol::table object = lua.create_table();
        object[sol::metatable_key] = lua.create_table_with("__index", classes.at(k));
        object["entity"] = handle(world,e);
        object["world"] = WorldApi{api};
        auto behaviour = std::make_unique<LuaBehaviour>(object, classes.at(k));
        ScriptBehaviour* entry = behaviour.get();
        instances.emplace(e, Instance{handle(world,e), std::move(behaviour), false});
        entry->on_create();
    }
    void remove(Entity e) {
        auto it = instances.find(e);
        if (it == instances.end()) return;
        if (it->second.entity.valid()) {
            try { it->second.behaviour->on_destroy(); } catch (const std::exception& x) { report(x); }
        }
        instances.erase(it);
    }
};
ScriptSystem::ScriptSystem(World& world) : impl_(std::make_unique<Impl>(world)) {}
ScriptSystem::~ScriptSystem() {
    stop();
    impl_.reset(); // sol::state closes after instances, prototypes and environments.
    LOG_INFO("Lua runtime shut down");
}
bool ScriptSystem::running() const { return impl_->api->active; }
const std::string& ScriptSystem::error() const { return impl_->error; }
const std::string& ScriptSystem::status() const { return impl_->api->status; }
size_t ScriptSystem::instanceCount() const { return impl_->instances.size(); }
void ScriptSystem::attachDefaults(Entity e) {
    auto& p = *impl_;
    try {
        auto& c = p.world.getComponent<ScriptComponent>(e); auto k = p.key(c);
        if (!p.classes.count(k)) p.classes[k] = p.load(c);
        p.defaults(c, p.classes.at(k));
    } catch (const std::exception& x) { p.report(x); }
}
bool ScriptSystem::reload() {
    auto& p = *impl_;
    if (running()) { p.error = "Stop Play before reloading scripts"; return false; }
    try {
        std::map<std::string, sol::table> replacement;
        std::map<Entity, ScriptComponent> components;
        for (Entity e : p.world.getEntities()) if (p.world.hasComponent<ScriptComponent>(e)) {
            auto c = p.world.getComponent<ScriptComponent>(e); const auto k = p.key(c);
            if (!replacement.count(k)) replacement[k] = p.load(c);
            p.defaults(c, replacement[k]); components.emplace(e, std::move(c));
        }
        p.classes.swap(replacement);
        for (auto& [e,c] : components) p.world.getComponent<ScriptComponent>(e) = std::move(c);
        p.error.clear();
    } catch (const std::exception& x) { p.report(x); return false; }
    p.lua.collect_garbage();
    return true;
}
bool ScriptSystem::start() {
    stop();
    if (!reload()) return false;
    auto& p = *impl_; p.api->active = true; p.api->spawn = spawnPrefab; p.api->pressed = inputPressed;
    p.api->status = "Play: Space starts a wave; F sends a defense pulse.";
    for (Entity e : p.world.getEntities()) if (p.world.hasComponent<ScriptComponent>(e)) {
        try { p.create(e); } catch (const std::exception& x) { p.report(x); stop(); return false; }
    }
    return true;
}
void ScriptSystem::stop() {
    auto& p = *impl_;
    if (!p.api->active && p.instances.empty()) return;
    p.api->active = false; // Retained World proxies cannot spawn during teardown.
    while (!p.instances.empty()) p.remove(p.instances.begin()->first);
    p.api = std::make_shared<ApiState>(); p.api->world = &p.world;
    p.lua.collect_garbage();
}
void ScriptSystem::update(float dt, bool allowInput) {
    auto& p = *impl_; if (!running()) return;
    p.api->allowInput = allowInput;
    const auto entities = p.world.getEntities(); // Spawning cannot invalidate this iteration.
    for (Entity e : entities) if (p.world.hasComponent<ScriptComponent>(e) && !p.api->destroyed.count(e)) {
        try {
            auto old = p.instances.find(e);
            if (old != p.instances.end() && !old->second.entity.valid()) p.instances.erase(old);
            if (!p.instances.count(e)) p.create(e);
            auto& i = p.instances.at(e);
            if (!i.failed && i.entity.valid()) i.behaviour->on_update(dt);
        } catch (const std::exception& x) {
            p.report(x);
            if (p.instances.count(e)) p.instances.at(e).failed = true;
            else p.api->destroyed.insert(e);
        }
    }
    auto destroyed = std::move(p.api->destroyed); p.api->destroyed.clear();
    for (Entity e : destroyed) { p.remove(e); p.world.destroyEntity(e); }
    for (auto it = p.instances.begin(); it != p.instances.end();) {
        if (!it->second.entity.valid()) it = p.instances.erase(it); else ++it;
    }
    lua_gc(p.lua.lua_state(), LUA_GCSTEP, 32);
}
