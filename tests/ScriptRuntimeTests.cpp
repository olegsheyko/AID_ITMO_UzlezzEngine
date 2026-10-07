// Рантайм скриптов сверх базовой механики: место ошибки из C++-биндингов, print в лог,
// приведение типов полей, лимиты времени и памяти (sandbox).
#include "scripting/ScriptSystem.h"
#include "scripting/ScriptWatcher.h"
#include "ecs/Components.h"
#include "core/Logger.h"
#include "jobs/JobSystem.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <functional>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
std::filesystem::path fixtures;

void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
std::string write(const std::string& name, const std::string& text) {
    const auto path = fixtures / name;
    std::ofstream out(path);
    out << text;
    require(bool(out), "fixture write failed: " + path.string());
    return path.string();
}
Entity attach(World& world, const std::string& path, const std::string& className,
    std::map<std::string, ScriptValue> fields = {}) {
    const Entity entity = world.createEntity();
    world.addComponent<Transform>(entity);
    world.addComponent<ScriptComponent>(entity, ScriptComponent{path, className, {}, std::move(fields)});
    return entity;
}
template<class T> T field(World& world, Entity entity, const std::string& name) {
    return std::get<T>(world.getComponent<ScriptComponent>(entity).fields.at(name));
}
// Сообщения лога после cursor одной строкой.
std::string logSince(std::uint64_t& cursor) {
    std::vector<Logger::Entry> entries;
    Logger::getInstance().fetchSince(cursor, entries);
    std::string text;
    for (const auto& entry : entries) text += entry.message + "\n";
    return text;
}
bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void bindingErrorsHaveLocation() {
    World world;
    ScriptSystem scripts(world);
    const auto path = write("typo.lua", R"(Typo = uzlezz.behaviour {fields = {health = 1}}
function Typo:on_update(dt)
    return self.entity:get_field('helth')
end
)");
    attach(world, path, "Typo");
    require(scripts.start(), scripts.error());
    scripts.update(0.1f, true);
    require(contains(scripts.error(), "typo.lua:3:"), "C++ binding error must carry file:line, got: " + scripts.error());
    require(contains(scripts.error(), "unknown field 'helth'"), "unknown field must be named: " + scripts.error());
    require(scripts.stats().failedInstances == 1, "failing instance is disabled");
    scripts.update(0.1f, true); // отключённый экземпляр больше не зовётся, движок жив
}

void printGoesToLogAndFilesAreClosed() {
    World world;
    ScriptSystem scripts(world);
    const auto path = write("print.lua", R"(Printer = uzlezz.behaviour {fields = {}}
function Printer:on_create()
    assert(dofile == nil and loadfile == nil, 'file access must be closed')
    print('hello', 42, nil)
end
)");
    attach(world, path, "Printer");
    std::uint64_t cursor = 0;
    logSince(cursor);
    require(scripts.start(), scripts.error());
    const std::string log = logSince(cursor);
    require(contains(log, "print.lua:4: hello\t42\tnil"), "print must reach the engine log with its location:\n" + log);
}

void fieldTypesAreCoerced() {
    World world;
    ScriptSystem scripts(world);
    const auto path = write("types.lua", R"(Types = uzlezz.behaviour {fields = {count = 1, speed = 1.0, flag = true}}
function Types:on_create()
    self.entity:set_field('speed', 5)
    self.entity:set_field('count', 4.0)
end
function Types:on_update(dt)
    self.entity:set_field('count', 2.5)
end
)");
    // Префаб с "speed": 2 и "count": 3.0 — так пишут руками в JSON.
    const Entity entity = attach(world, path, "Types", {{"speed", 2}, {"count", 3.0}, {"flag", std::string("yes")}, {"legacy", 1}});
    std::uint64_t cursor = 0;
    logSince(cursor);
    require(scripts.reload(), "lenient field reconciliation must not fail reload: " + scripts.error());
    require(field<double>(world, entity, "speed") == 2.0, "integer widens to number");
    require(field<int>(world, entity, "count") == 3, "integral number narrows to integer");
    require(field<bool>(world, entity, "flag"), "mismatched type resets to the declared default");
    require(!world.getComponent<ScriptComponent>(entity).fields.count("legacy"), "undeclared field is dropped");
    const std::string log = logSince(cursor);
    require(contains(log, "'flag'") && contains(log, "'legacy'"), "reconciliation must warn:\n" + log);
    require(scripts.start(), scripts.error());
    require(field<double>(world, entity, "speed") == 5.0 && field<int>(world, entity, "count") == 4, "set_field coerces numbers");
    scripts.update(0.1f, true);
    require(contains(scripts.error(), "types.lua:7:") && contains(scripts.error(), "expects integer"),
        "fractional value into an integer field must fail with location: " + scripts.error());
}

void runawayScriptIsInterrupted() {
    World world;
    ScriptSystem scripts(world);
    scripts.setLimits(ScriptLimits{50, ScriptLimits{}.memoryBytes});
    const auto loop = write("loop.lua", R"(Loop = uzlezz.behaviour {fields = {}}
function Loop:on_update(dt)
    local n = 0
    while true do n = n + 1 end
end
)");
    const auto counter = write("counter.lua", R"(Counter = uzlezz.behaviour {fields = {ticks = 0}}
function Counter:on_update(dt)
    self.entity:set_field('ticks', self.entity:get_field('ticks') + 1)
end
)");
    attach(world, loop, "Loop");
    const Entity ticks = attach(world, counter, "Counter");
    require(scripts.start(), scripts.error());
    const auto started = std::chrono::steady_clock::now();
    scripts.update(0.1f, true);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    require(elapsed < std::chrono::seconds(2), "infinite loop must be interrupted by the time budget");
    require(contains(scripts.error(), "loop.lua:4:") && contains(scripts.error(), "50 ms time budget"),
        "budget error must point at the loop: " + scripts.error());
    for (int i = 0; i < 10; ++i) scripts.update(0.1f, true);
    require(field<int>(world, ticks, "ticks") == 11, "other instances keep running");
    require(scripts.stats().failedInstances == 1, "only the runaway instance is disabled");
    scripts.stop();

    const auto topLevel = write("toplevel.lua", "Spin = uzlezz.behaviour {fields = {}}\nwhile true do end\n");
    world.clear();
    attach(world, topLevel, "Spin");
    require(!scripts.reload(), "endless file body must fail reload");
    require(contains(scripts.error(), "toplevel.lua:2:"), "load budget error must point at the file: " + scripts.error());
}

void heapLimitIsEnforced() {
    World world;
    ScriptSystem scripts(world);
    const auto path = write("hog.lua", R"(Hog = uzlezz.behaviour {fields = {}}
function Hog:on_update(dt)
    self.blob = string.rep('x', 1 << 30)
end
)");
    attach(world, path, "Hog");
    require(scripts.start(), scripts.error());
    scripts.update(0.1f, true);
    require(contains(scripts.error(), "Hog:on_update") && contains(scripts.error(), "heap limit"),
        "allocation over the limit must fail as a script error: " + scripts.error());
    const ScriptStats stats = scripts.stats();
    require(stats.memoryBytes > 0 && stats.memoryBytes < (std::size_t(32) << 20), "heap stays small after the refused allocation");
    require(stats.instances == 1, "stats count instances");
}
// Версии одного поведения для hot reload. Состояние self.* выводится в поля, чтобы тест его видел.
std::string mover(const std::string& label, const std::string& speedFactor, const std::string& extra = {}) {
    return "Mover = uzlezz.behaviour {fields = {speed = 1.0, ticks = 0, label = ''}}\n"
        "function Mover:on_create() self.ticks = 0; self.label = '" + label + "' end\n"
        "function Mover:on_update(dt)\n"
        "    self.ticks = self.ticks + 1\n"
        "    local p = self.entity:get_position()\n"
        "    p.x = p.x + self.entity:get_field('speed') * " + speedFactor + " * dt\n"
        "    self.entity:set_position(p)\n"
        "    self.entity:set_field('ticks', self.ticks)\n"
        "    self.entity:set_field('label', self.label)\n"
        "end\n" + extra;
}
float positionX(World& world, Entity entity) { return world.getComponent<Transform>(entity).position.x; }

void hotReloadInPlay() {
    World world;
    ScriptSystem scripts(world);
    const auto path = write("mover.lua", mover("v1", "1"));
    const Entity e = attach(world, path, "Mover");
    require(scripts.start(), scripts.error());
    for (int i = 0; i < 5; ++i) scripts.update(1.0f, true);
    require(positionX(world, e) == 5.0f && field<int>(world, e, "ticks") == 5, "v1 runs");

    // L2: новый код сразу в Play, счётчик тиков перенесён, изменённое в коде начальное значение — нет.
    HotReloadReport report = scripts.hotReload(path, mover("v2", "10"), ReloadMode::KeepState);
    require(report.ok && report.instances == 1 && report.keptFields >= 1, "keep-state reload: " + report.error);
    scripts.update(1.0f, true);
    require(positionX(world, e) == 15.0f, "new on_update runs without Stop");
    require(field<int>(world, e, "ticks") == 6, "unchanged state survives (L2)");
    require(field<std::string>(world, e, "label") == "v2", "a field whose initial value changed in code takes the new value");

    // L1: состояние заново из on_create.
    report = scripts.hotReload(path, mover("v2", "10"), ReloadMode::Reset);
    require(report.ok && report.keptFields == 0, "reset reload: " + report.error);
    scripts.update(1.0f, true);
    require(field<int>(world, e, "ticks") == 1, "reset mode restarts instance state (L1)");

    // Своя миграция состояния.
    report = scripts.hotReload(path, mover("v3", "10", "function Mover:on_reload(old) self.ticks = old.ticks + 100 end\n"));
    require(report.ok && report.migrated == 1, "on_reload migration: " + report.error);
    scripts.update(1.0f, true);
    require(field<int>(world, e, "ticks") == 102, "on_reload receives the previous instance");

    // Сломанный файл: старый код продолжает работать, ошибка с файлом и строкой.
    const float before = positionX(world, e);
    report = scripts.hotReload(path, "Mover = uzlezz.behaviour { fields = \n");
    require(!report.ok && contains(report.error, "mover.lua:"), "syntax error must be reported with location: " + report.error);
    scripts.update(1.0f, true);
    require(positionX(world, e) == before + 10.0f, "previous code keeps running after a failed reload");

    // Новый on_create падает — экземпляр остаётся на прежнем классе.
    report = scripts.hotReload(path, mover("v4", "1", "function Mover:on_create() error('boom') end\n"));
    require(!report.ok && report.failedInstances == 1 && contains(report.error, "boom"), "failing on_create is contained");
    scripts.update(1.0f, true);
    require(positionX(world, e) == before + 20.0f, "instance keeps the previous implementation");

    // Ошибка в update отключает экземпляр, исправленный файл его оживляет.
    report = scripts.hotReload(path, mover("v5", "1", "function Mover:on_update(dt) error('broken update') end\n"));
    scripts.update(1.0f, true);
    require(scripts.stats().failedInstances == 1, "runtime error disables the instance");
    report = scripts.hotReload(path, mover("v5", "1"));
    require(report.ok && report.revived == 1, "fixed code revives the disabled instance");
    const float revived = positionX(world, e);
    scripts.update(1.0f, true);
    require(positionX(world, e) == revived + 1.0f && scripts.stats().failedInstances == 0, "revived instance updates");

    // Поле добавили в fields — компонент получает значение по умолчанию прямо в Play.
    std::string withBoost = mover("v6", "1");
    withBoost.replace(withBoost.find("label = ''"), 10, "label = '', boost = 2");
    report = scripts.hotReload(path, withBoost);
    require(report.ok && field<int>(world, e, "boost") == 2, "new declared field appears in Play");

    // Серия перезагрузок подряд, рабочая и сломанная версии по очереди: без падений и роста кучи.
    scripts.update(1.0f, true);
    const std::size_t baseline = scripts.stats().memoryBytes;
    for (int i = 0; i < 200; ++i) {
        report = scripts.hotReload(path, i % 2 ? mover("v" + std::to_string(i), "1") : "Mover = {\n");
        require(report.ok == (i % 2 == 1), "alternating reload result");
        scripts.update(0.016f, true);
    }
    require(scripts.instanceCount() == 1 && scripts.stats().failedInstances == 0, "instances stay stable over 200 reloads");
    require(scripts.stats().memoryBytes < baseline + (std::size_t(2) << 20), "Lua heap does not grow with reloads");

    // Файл, который сцене не нужен, ничего не трогает.
    require(scripts.hotReload(fixtures.string() + "/unused.lua", "x = 1").ok, "unused file is ignored");
}

// Вотчер: задача job system находит изменение, ждёт стабильной метки, проверяет синтаксис.
void watcherUsesJobSystem() {
    const auto dir = fixtures / "watched";
    std::filesystem::create_directories(dir);
    const auto path = (dir / "w.lua").string();
    auto save = [&](const std::string& text) {
        std::ofstream(path, std::ios::trunc) << text;
        static int bump = 0;
        // Метка гарантированно другая, даже если две записи попали в один тик файловой системы.
        std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(++bump));
    };
    save("Watched = 1\n");
    ScriptWatcher watcher;
    watcher.interval = 0.0;
    const std::vector<std::string> roots{dir.string()};
    auto waitForChange = [&]() {
        double now = 0.0;
        for (int i = 0; i < 400; ++i) {
            watcher.poll(now += 1.0, roots, {});
            auto changes = watcher.drain();
            if (!changes.empty()) return changes;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        throw std::runtime_error("watcher did not report the change");
    };
    watcher.scanNow(roots, {});
    require(watcher.drain().empty(), "first scan is a baseline, not a change");

    save("Watched = 2\n");
    auto changes = waitForChange();
    require(changes.size() == 1 && changes[0].ok && changes[0].source == "Watched = 2\n", "valid change with its source");
    require(contains(changes[0].path, "watched/w.lua"), "path is normalized: " + changes[0].path);
    require(watcher.scans() >= 2, "change is confirmed on a second scan (debounce)");

    save("Watched = \n  = 3\n");
    changes = waitForChange();
    require(changes.size() == 1 && !changes[0].ok && contains(changes[0].error, "w.lua:2:"),
        "syntax error is caught on the worker with location: " + (changes.empty() ? std::string() : changes[0].error));
}

// Аннотации для IDE (tools/lua-stubs/uzlezz.lua) не должны отставать от биндингов и обещать лишнего.
void ideStubsMatchBindings() {
    World world;
    ScriptSystem scripts(world);
    std::ifstream in("tools/lua-stubs/uzlezz.lua");
    require(bool(in), "stub file not found (tests run from the source root)");
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string stub = buffer.str();
    const auto bound = scripts.apiNames();
    require(bound.size() >= 15, "bindings introspection returned too few names");
    for (const std::string& name : bound) {
        const auto colon = name.find(':');
        const std::string type = name.substr(0, colon), member = name.substr(colon + 1);
        const std::string luaClass = type == "ScriptBehaviour" ? "Behaviour" : type;
        const auto begin = stub.find("---@class uzlezz." + luaClass + "\n");
        require(begin != std::string::npos, "stub lacks class uzlezz." + luaClass);
        const auto end = stub.find("---@class", begin + 1);
        const std::string section = stub.substr(begin, end - begin);
        const bool documented = contains(stub, "function " + type + ":" + member + "(") ||
            contains(section, "---@field " + member + " ");
        require(documented, "stub does not describe bound " + name);
    }
    const std::regex method(R"(function (\w+):(\w+)\()");
    for (auto it = std::sregex_iterator(stub.begin(), stub.end(), method); it != std::sregex_iterator(); ++it) {
        const std::string name = (*it)[1].str() + ":" + (*it)[2].str();
        require(std::find(bound.begin(), bound.end(), name) != bound.end(), "stub promises an unbound method " + name);
    }
}
} // namespace

int main(int argc, char** argv) {
    fixtures = std::filesystem::absolute(argc > 1 ? argv[1] : "script-runtime-fixtures");
    std::filesystem::create_directories(fixtures);
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"binding errors have location", bindingErrorsHaveLocation},
        {"print goes to log, file access closed", printGoesToLogAndFilesAreClosed},
        {"field types are coerced", fieldTypesAreCoerced},
        {"runaway script is interrupted", runawayScriptIsInterrupted},
        {"heap limit is enforced", heapLimitIsEnforced},
        {"IDE stubs match bindings", ideStubsMatchBindings},
        {"hot reload in Play (L1/L2)", hotReloadInPlay},
        {"watcher uses the job system", watcherUsesJobSystem},
    };
    JobSystem::getInstance().init();
    int failed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    JobSystem::getInstance().shutdown();
    return failed == 0 ? 0 : 1;
}
