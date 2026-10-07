#include "scripting/ScriptSystem.h"
#include "ecs/Components.h"
#include "core/Logger.h"
#include <sol/sol.hpp>
#include <tracy/Tracy.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <algorithm>
#include <filesystem>
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
const char* typeName(const ScriptValue& value) {
    static const char* names[] = {"boolean", "integer", "number", "string"};
    return names[value.index()];
}
// Lua 5.4 отличает 2 от 2.0. Целое расширяется до числа, число сужается до целого без дробной части.
bool coerce(ScriptValue& value, const ScriptValue& like) {
    if (value.index() == like.index()) return true;
    if (std::holds_alternative<double>(like) && std::holds_alternative<int>(value)) {
        value = static_cast<double>(std::get<int>(value));
        return true;
    }
    if (std::holds_alternative<int>(like) && std::holds_alternative<double>(value)) {
        const double number = std::get<double>(value);
        if (std::trunc(number) == number && number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max()) {
            value = static_cast<int>(number);
            return true;
        }
    }
    return false;
}
ScriptComponent& scriptOf(const EntityHandle& h) {
    h.check();
    if (!h.world->hasComponent<ScriptComponent>(h.id)) throw std::runtime_error("Entity has no script fields");
    return h.world->getComponent<ScriptComponent>(h.id);
}
ScriptValue& fieldOf(ScriptComponent& c, const std::string& name) {
    auto it = c.fields.find(name);
    if (it == c.fields.end()) throw std::runtime_error(c.className + ": unknown field '" + name + "'");
    return it->second;
}
// Исключение из C++-биндинга: sol2 по умолчанию отдаёт в Lua только what(), без места вызова.
int locatedException(lua_State* L, sol::optional<const std::exception&>, sol::string_view what) {
    luaL_where(L, 1);
    lua_pushlstring(L, what.data(), what.size());
    lua_concat(L, 2);
    return 1;
}
// Обработчик ошибок protected-вызовов: к сообщению добавляется стек Lua.
int traceback(lua_State* L) {
    const char* message = lua_tostring(L, 1);
    if (!message) message = luaL_tolstring(L, 1, nullptr);
    luaL_traceback(L, L, message, 1);
    return 1;
}
int print(lua_State* L) {
    luaL_where(L, 1);
    std::string line = lua_tostring(L, -1);
    lua_pop(L, 1);
    const int count = lua_gettop(L);
    for (int i = 1; i <= count; ++i) {
        size_t size = 0;
        const char* text = luaL_tolstring(L, i, &size);
        if (i > 1) line += '\t'; // luaL_where уже оканчивается на ": "
        line.append(text, size);
        lua_pop(L, 1);
    }
    LOG_INFO("Lua: " + line);
    return 0;
}
void checkPosition(Vec3 p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
        throw std::runtime_error("Position must be finite");
}
// Sandbox: потолок кучи Lua через собственный аллокатор и бюджет времени на один вызов скрипта.
// Живёт дольше sol::state — аллокатор нужен до последнего free.
struct Sandbox {
    std::size_t used = 0, peak = 0, limit = ScriptLimits{}.memoryBytes;
    int budgetMs = ScriptLimits{}.timeBudgetMs;
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
};
void* allocate(void* data, void* block, size_t oldSize, size_t newSize) {
    auto& sandbox = *static_cast<Sandbox*>(data);
    const size_t old = block ? oldSize : 0; // без блока oldSize — код типа объекта
    if (newSize == 0) {
        std::free(block);
        sandbox.used -= old;
        return nullptr;
    }
    // Lua требует, чтобы сжатие не отказывало; лимит проверяется только при росте.
    if (newSize > old && sandbox.used - old + newSize > sandbox.limit) return nullptr;
    void* next = std::realloc(block, newSize);
    if (next) {
        sandbox.used = sandbox.used - old + newSize;
        sandbox.peak = std::max(sandbox.peak, sandbox.used);
    }
    return next;
}
// Count-хук раз в 1000 инструкций VM: вызов, превысивший бюджет, прерывается ошибкой Lua.
void budgetHook(lua_State* L, lua_Debug*) {
    void* data = nullptr;
    lua_getallocf(L, &data);
    const auto& sandbox = *static_cast<Sandbox*>(data);
    if (std::chrono::steady_clock::now() <= sandbox.deadline) return;
    luaL_where(L, 0); // хук не создаёт кадр: уровень 0 — сама зациклившаяся функция
    lua_pushfstring(L, "script exceeded the %d ms time budget", sandbox.budgetMs);
    lua_concat(L, 2);
    lua_error(L);
}
// Бюджет на время вызова. Вложенный вызов (spawn из on_update → on_create) не продлевает внешний.
class Deadline {
public:
    explicit Deadline(Sandbox& sandbox) : sandbox_(sandbox), previous_(sandbox.deadline) {
        sandbox.deadline = std::min(previous_, std::chrono::steady_clock::now() + std::chrono::milliseconds(sandbox.budgetMs));
    }
    ~Deadline() { sandbox_.deadline = previous_; }
    Deadline(const Deadline&) = delete;
    Deadline& operator=(const Deadline&) = delete;
private:
    Sandbox& sandbox_;
    std::chrono::steady_clock::time_point previous_;
};
// C++ virtual adapter; Lua instances inherit prototypes from uzlezz.behaviour().
class LuaBehaviour final : public ScriptBehaviour {
public:
    LuaBehaviour(sol::table object, sol::table prototype, sol::reference handler, Sandbox& sandbox, const std::string& className)
        : object_(std::move(object)), prototype_(std::move(prototype)), handler_(std::move(handler)), sandbox_(sandbox),
          labels_{className + ":on_create", className + ":on_update", className + ":on_destroy", className + ":on_reload"} {}
    void on_create() override { call(0, "on_create"); }
    void on_update(float dt) override { call(1, "on_update", dt); }
    void on_destroy() override { call(2, "on_destroy"); }
    // Hot reload в Play: новый экземпляр сам переносит нужное из старого (необязательный callback).
    void on_reload(const sol::table& previous) { call(3, "on_reload", previous); }
    bool has(const char* name) const { return prototype_.raw_get<sol::object>(name).get_type() == sol::type::function; }
    const sol::table& object() const { return object_; }
private:
    template<class... Args> void call(int slot, const char* name, Args... args) {
        sol::object method = prototype_.raw_get<sol::object>(name);
        if (!method.valid() || method == sol::lua_nil) return;
        // Зона на каждый entry point: на трейсе видно, сколько стоит тик Enemy, Waves и Core.
        ZoneScopedN("Lua callback");
        ZoneName(labels_[slot].data(), labels_[slot].size());
        sol::protected_function function = method.as<sol::protected_function>();
        function.set_error_handler(handler_);
        Deadline deadline(sandbox_);
        sol::protected_function_result result = function(object_, args...);
        if (result.valid()) return;
        // Для нехватки памяти Lua не зовёт обработчик ошибок, места в сообщении нет — называем вызов.
        if (result.status() == sol::call_status::memory)
            throw std::runtime_error(labels_[slot] + ": Lua heap limit exceeded (" + std::to_string(sandbox_.limit >> 20) + " MB)");
        sol::error error = result; throw error;
    }
    sol::table object_, prototype_;
    sol::reference handler_;
    Sandbox& sandbox_;
    std::string labels_[4];
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
            return toLua(state, fieldOf(scriptOf(h), name));
        },
        "set_field", [](const EntityHandle& h, const std::string& name, sol::object value) {
            auto& component = scriptOf(h);
            auto& old = fieldOf(component, name);
            auto next = fromLua(value);
            if (!coerce(next, old))
                throw std::runtime_error(component.className + ": field '" + name + "' expects " + typeName(old) + ", got " + typeName(next));
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
// Имена, которые биндинги кладут в метатаблицу usertype, без служебных ключей sol2.
template<class T> void collectApi(lua_State* L, const std::string& type, std::vector<std::string>& out) {
    luaL_getmetatable(L, sol::usertype_traits<T>::metatable().c_str());
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        if (lua_type(L, -2) == LUA_TSTRING) {
            const std::string name = lua_tostring(L, -2);
            if (name.rfind("__", 0) != 0 && name != "new" && name != "class_check" && name != "class_cast")
                out.push_back(type + ":" + name);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}
} // namespace

struct ScriptSystem::Impl {
    Sandbox sandbox; // аллокатор state'а: объявлен раньше и переживает его
    // Declared before every sol reference, destroyed after them.
    sol::state lua{&sol::default_at_panic, &allocate, &sandbox};
    World& world;
    std::shared_ptr<ApiState> api = std::make_shared<ApiState>();
    std::map<std::string, sol::table> classes;
    struct Instance {
        EntityHandle entity;
        std::unique_ptr<LuaBehaviour> behaviour;
        bool failed = false;
        sol::table initial; // self.* сразу после on_create — чтобы при hot reload понять, что поменяли в коде
    };
    std::map<Entity, Instance> instances;
    std::string error;
    sol::reference errorHandler;
    double updateMs = 0.0;
    explicit Impl(World& w) : world(w) {
        api->world = &w;
        lua_sethook(lua.lua_state(), &budgetHook, LUA_MASKCOUNT, 1000);
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);
        lua.set_exception_handler(&locatedException);
        lua.set_function("print", &print);
        // Скрипты не читают файлы в обход движка.
        lua["dofile"] = sol::lua_nil;
        lua["loadfile"] = sol::lua_nil;
        errorHandler = sol::make_reference(lua.lua_state(), &traceback);
        bindEngine(lua);
        LOG_INFO("Lua runtime initialized: " LUA_RELEASE " (sol2, statically linked)");
    }
    // В строку ошибки для UI — первая строка с файл:строка, в лог — вместе со стеком.
    void report(const std::exception& e) {
        const std::string full = e.what();
        error = full.substr(0, full.find('\n'));
        LOG_ERROR("Lua: " + full);
    }
    std::string key(const ScriptComponent& c) const { return c.path + ":" + c.className; }
    // Выполнить файл (или уже прочитанный вотчером текст) в собственном environment.
    sol::environment run(const std::string& path, const std::string* source) {
        ZoneScopedN("Lua: load file");
        sol::environment environment(lua, sol::create, lua.globals());
        Deadline deadline(sandbox);
        auto result = source
            ? lua.safe_script(*source, environment, sol::script_pass_on_error, "@" + path, sol::load_mode::text)
            : lua.safe_script_file(path, environment, sol::script_pass_on_error, sol::load_mode::text);
        if (!result.valid()) { sol::error error = result; throw error; }
        return environment;
    }
    sol::table extract(const sol::environment& environment, const std::string& path, const std::string& className) {
        sol::object object = environment[className];
        if (!object.is<sol::table>()) throw std::runtime_error(path + ": missing behaviour class " + className);
        auto cls = object.as<sol::table>();
        sol::object base = lua["uzlezz"]["ScriptBehaviour"];
        if (cls.raw_get<sol::object>("base") != base)
            throw std::runtime_error(path + ": class must extend uzlezz.ScriptBehaviour using uzlezz.behaviour");
        sol::object fields = cls.raw_get<sol::object>("fields");
        if (!fields.is<sol::table>()) throw std::runtime_error(path + ": missing fields table");
        for (const auto& item : fields.as<sol::table>()) {
            if (item.first.get_type() != sol::type::string) throw std::runtime_error(path + ": field name must be a string");
            fromLua(item.second);
        }
        for (const char* name : {"on_create", "on_update", "on_destroy", "on_reload"}) {
            sol::object method = cls.raw_get<sol::object>(name);
            if (method != sol::lua_nil && method.get_type() != sol::type::function)
                throw std::runtime_error(path + ": callback must be a function: " + name);
        }
        return cls;
    }
    sol::table load(const ScriptComponent& c) { return extract(run(c.path, nullptr), c.path, c.className); }
    sol::table makeObject(const sol::table& cls, Entity e) {
        sol::table object = lua.create_table();
        object[sol::metatable_key] = lua.create_table_with("__index", cls);
        object["entity"] = handle(world, e);
        object["world"] = WorldApi{api};
        return object;
    }
    static bool service(const sol::object& key) {
        if (key.get_type() != sol::type::string) return false;
        const auto name = key.as<std::string>();
        return name == "entity" || name == "world";
    }
    // Поверхностная копия self.* без entity/world.
    sol::table snapshot(const sol::table& object) {
        sol::table copy = lua.create_table();
        for (const auto& [key, value] : object) {
            if (!service(key)) copy.raw_set(key, value);
        }
        return copy;
    }
    bool rawEqual(const sol::object& a, const sol::object& b) {
        lua_State* L = lua.lua_state();
        a.push(L);
        b.push(L);
        const bool equal = lua_rawequal(L, -1, -2) != 0;
        lua_pop(L, 2);
        return equal;
    }
    // L2: перенос состояния в новый экземпляр. Переносится поле, которое есть в обоих с тем же типом
    // и чьё начальное значение в коде не поменяли (иначе правка «cooldown = 5» не была бы видна).
    std::size_t transfer(const sol::table& previous, const sol::table& previousInitial, sol::table& next, const sol::table& nextInitial) {
        std::size_t kept = 0;
        for (const auto& [key, value] : previous) {
            if (service(key)) continue;
            const sol::object fresh = next.raw_get<sol::object>(key);
            if (fresh.get_type() == sol::type::lua_nil || fresh.get_type() != value.get_type()) continue;
            const sol::type type = value.get_type();
            if (previousInitial.valid() && (type == sol::type::number || type == sol::type::string || type == sol::type::boolean)) {
                const sol::object before = previousInitial.raw_get<sol::object>(key);
                const sol::object after = nextInitial.raw_get<sol::object>(key);
                if (before.get_type() == type && after.get_type() == type && !rawEqual(before, after)) continue;
            }
            next.raw_set(key, value);
            ++kept;
        }
        return kept;
    }
    // Поля компонента приводятся к объявлению класса: новые получают значение по умолчанию,
    // удалённые из Lua выбрасываются, поле со сменившимся типом сбрасывается. Reload от этого не падает.
    void defaults(ScriptComponent& c, const sol::table& cls) {
        sol::table defaults = cls["fields"];
        const std::string owner = c.prefab.empty() ? c.path : c.prefab;
        for (const auto& item : defaults) {
            const auto name = item.first.as<std::string>();
            const auto value = fromLua(item.second);
            auto it = c.fields.find(name);
            if (it == c.fields.end()) {
                c.fields[name] = value;
            } else if (!coerce(it->second, value)) {
                LOG_WARN("Lua: " + owner + ": field '" + name + "' is " + typeName(it->second) + ", class " + c.className +
                    " declares " + typeName(value) + "; reset to default");
                it->second = value;
            }
        }
        for (auto it = c.fields.begin(); it != c.fields.end();) {
            if (defaults.raw_get<sol::object>(it->first) != sol::lua_nil) { ++it; continue; }
            LOG_WARN("Lua: " + owner + ": field '" + it->first + "' is not declared by " + c.className + "; dropped");
            it = c.fields.erase(it);
        }
    }
    void create(Entity e) {
        auto& c = world.getComponent<ScriptComponent>(e);
        const auto k = key(c);
        if (!classes.count(k)) classes[k] = load(c);
        defaults(c, classes.at(k));
        sol::table object = makeObject(classes.at(k), e);
        auto behaviour = std::make_unique<LuaBehaviour>(object, classes.at(k), errorHandler, sandbox, c.className);
        LuaBehaviour* entry = behaviour.get();
        auto& instance = instances.emplace(e, Instance{handle(world,e), std::move(behaviour), false, {}}).first->second;
        entry->on_create();
        instance.initial = snapshot(object);
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
void ScriptSystem::clearError() { impl_->error.clear(); }
bool ScriptSystem::attachDefaults(Entity e) {
    auto& p = *impl_;
    try {
        auto& c = p.world.getComponent<ScriptComponent>(e); auto k = p.key(c);
        if (!p.classes.count(k)) p.classes[k] = p.load(c);
        p.defaults(c, p.classes.at(k));
        return true;
    } catch (const std::exception& x) { p.report(x); return false; }
}
void ScriptSystem::setLimits(const ScriptLimits& limits) {
    impl_->sandbox.budgetMs = std::max(1, limits.timeBudgetMs);
    impl_->sandbox.limit = limits.memoryBytes;
}
ScriptLimits ScriptSystem::limits() const { return {impl_->sandbox.budgetMs, impl_->sandbox.limit}; }
ScriptStats ScriptSystem::stats() const {
    ScriptStats stats;
    stats.memoryBytes = impl_->sandbox.used;
    stats.peakMemoryBytes = impl_->sandbox.peak;
    stats.instances = impl_->instances.size();
    for (const auto& [entity, instance] : impl_->instances) stats.failedInstances += instance.failed ? 1 : 0;
    stats.updateMs = impl_->updateMs;
    return stats;
}
std::vector<std::string> ScriptSystem::apiNames() const {
    std::vector<std::string> names;
    lua_State* L = impl_->lua.lua_state();
    collectApi<ScriptBehaviour>(L, "ScriptBehaviour", names);
    collectApi<Vec3>(L, "Vec3", names);
    collectApi<EntityHandle>(L, "Entity", names);
    collectApi<WorldApi>(L, "World", names);
    std::sort(names.begin(), names.end());
    return names;
}
bool ScriptSystem::reload() {
    ZoneScopedN("Lua: reload");
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
namespace {
std::string normalizePath(const std::string& path) {
    return std::filesystem::path(path).lexically_normal().generic_string();
}
} // namespace
std::vector<std::string> ScriptSystem::scriptPaths() const {
    std::set<std::string> paths;
    for (const auto& item : impl_->classes) paths.insert(item.first.substr(0, item.first.rfind(':')));
    for (Entity e : impl_->world.getEntities())
        if (impl_->world.hasComponent<ScriptComponent>(e)) paths.insert(impl_->world.getComponent<ScriptComponent>(e).path);
    return {paths.begin(), paths.end()};
}
HotReloadReport ScriptSystem::hotReload(const std::string& path, const std::string& source, ReloadMode mode) {
    ZoneScopedN("Lua: hot reload");
    auto& p = *impl_;
    HotReloadReport report;
    const std::string file = normalizePath(path);
    // Классы этого файла: уже загруженные и те, что объявлены компонентами сцены. Ключ — как в classes.
    std::map<std::string, std::pair<std::string, std::string>> targets;
    for (const auto& item : p.classes) {
        const auto colon = item.first.rfind(':');
        const std::string classPath = item.first.substr(0, colon);
        if (normalizePath(classPath) == file) targets[item.first] = {classPath, item.first.substr(colon + 1)};
    }
    for (Entity e : p.world.getEntities()) {
        if (!p.world.hasComponent<ScriptComponent>(e)) continue;
        const auto& c = p.world.getComponent<ScriptComponent>(e);
        if (normalizePath(c.path) == file) targets[p.key(c)] = {c.path, c.className};
    }
    if (targets.empty()) { report.ok = true; return report; } // файл сцене не нужен
    // Сначала всё новое собирается и проверяется; при ошибке старые классы и экземпляры остаются.
    std::map<std::string, sol::table> replacement;
    try {
        const sol::environment environment = p.run(file, &source);
        for (const auto& [key, target] : targets) replacement[key] = p.extract(environment, target.first, target.second);
    } catch (const std::exception& x) {
        p.report(x);
        report.error = p.error;
        return report;
    }
    for (auto& [key, cls] : replacement) p.classes[key] = cls;
    report.classes = replacement.size();
    for (Entity e : p.world.getEntities()) {
        if (!p.world.hasComponent<ScriptComponent>(e)) continue;
        auto& c = p.world.getComponent<ScriptComponent>(e);
        if (normalizePath(c.path) == file) p.defaults(c, p.classes.at(p.key(c)));
    }
    if (running()) {
        for (auto& [e, instance] : p.instances) {
            if (!instance.entity.valid() || p.api->destroyed.count(e) || !p.world.hasComponent<ScriptComponent>(e)) continue;
            const auto& c = p.world.getComponent<ScriptComponent>(e);
            if (normalizePath(c.path) != file) continue;
            const sol::table& cls = p.classes.at(p.key(c));
            sol::table object = p.makeObject(cls, e);
            auto behaviour = std::make_unique<LuaBehaviour>(object, cls, p.errorHandler, p.sandbox, c.className);
            try {
                behaviour->on_create();
                const sol::table initial = p.snapshot(object);
                if (mode == ReloadMode::KeepState) {
                    if (behaviour->has("on_reload")) {
                        behaviour->on_reload(instance.behaviour->object());
                        ++report.migrated;
                    } else {
                        report.keptFields += p.transfer(instance.behaviour->object(), instance.initial, object, initial);
                    }
                }
                report.revived += instance.failed ? 1 : 0;
                instance.behaviour = std::move(behaviour);
                instance.initial = initial;
                instance.failed = false;
                ++report.instances;
            } catch (const std::exception& x) {
                p.report(x); // экземпляр остаётся на старом классе
                ++report.failedInstances;
            }
        }
    }
    report.ok = report.failedInstances == 0;
    if (report.ok) p.error.clear(); else report.error = p.error;
    p.lua.collect_garbage();
    return report;
}
bool ScriptSystem::start() {
    ZoneScopedN("Lua: start");
    stop();
    if (!reload()) return false;
    auto& p = *impl_; p.api->active = true; p.api->spawn = spawnPrefab; p.api->pressed = inputPressed;
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
    ZoneScopedN("Lua: update");
    const auto started = std::chrono::steady_clock::now();
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
    p.updateMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    TracyPlot("Lua memory KB", static_cast<int64_t>(p.sandbox.used / 1024));
    TracyPlot("Lua instances", static_cast<int64_t>(p.instances.size()));
}
