#include "scripting/ScriptWatcher.h"
#include <tracy/Tracy.hpp>
#include <lua.hpp>
#include <fstream>
#include <iterator>
#include <set>

namespace {
std::string normalize(const std::filesystem::path& path) {
    return path.lexically_normal().generic_string();
}
} // namespace

ScriptWatcher::~ScriptWatcher() {
    // Задача держит shared_ сама, но дождаться её честнее, чем оставлять работу после смерти владельца.
    if (job_.isValid() && !job_.isDone()) {
        JobSystem::getInstance().wait(job_);
    }
}

bool ScriptWatcher::validate(const std::string& path, const std::string& source, std::string& error) {
    lua_State* L = luaL_newstate();
    if (!L) {
        error = path + ": cannot create a Lua state for validation";
        return false;
    }
    const std::string chunk = "@" + path; // как у luaL_loadfile: ошибки вида "path:line: ..."
    const bool ok = luaL_loadbufferx(L, source.data(), source.size(), chunk.c_str(), "t") == LUA_OK;
    if (!ok) {
        const char* message = lua_tostring(L, -1);
        error = message ? message : path + ": unknown syntax error";
    }
    lua_close(L);
    return ok;
}

void ScriptWatcher::scan(Shared& shared, const std::vector<std::string>& roots, const std::vector<std::string>& paths) {
    ZoneScopedN("Lua watcher: scan");
    std::set<std::string> files;
    for (const std::string& path : paths) {
        files.insert(normalize(path));
    }
    std::error_code code;
    for (const std::string& root : roots) {
        for (std::filesystem::recursive_directory_iterator it(root, code), end; !code && it != end; it.increment(code)) {
            if (it->is_regular_file(code) && it->path().extension() == ".lua") {
                files.insert(normalize(it->path()));
            }
        }
        code.clear();
    }

    for (const std::string& path : files) {
        Stamp stamp;
        stamp.time = std::filesystem::last_write_time(path, code);
        if (code) { code.clear(); continue; }
        stamp.size = std::filesystem::file_size(path, code);
        if (code) { code.clear(); continue; }
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto known = shared.known.find(path);
            if (known == shared.known.end()) {
                shared.known.emplace(path, stamp); // первая встреча — базовая версия, не изменение
                continue;
            }
            if (known->second == stamp) {
                shared.pending.erase(path);
                continue;
            }
            auto pending = shared.pending.find(path);
            if (pending == shared.pending.end() || pending->second != stamp) {
                shared.pending[path] = stamp; // ждём, пока запись файла закончится
                continue;
            }
        }
        // Метка стабильна два опроса: читаем и проверяем без блокировки.
        Change change;
        change.path = path;
        {
            ZoneScopedN("Lua watcher: validate");
            std::ifstream in(path, std::ios::binary);
            if (in) {
                change.source.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                change.ok = validate(path, change.source, change.error);
            } else {
                change.error = path + ": cannot read file";
            }
        }
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.known[path] = stamp;
        shared.pending.erase(path);
        shared.ready.push_back(std::move(change));
    }
    std::lock_guard<std::mutex> lock(shared.mutex);
    ++shared.scans;
}

void ScriptWatcher::poll(double nowSeconds, const std::vector<std::string>& roots, const std::vector<std::string>& paths) {
    if (!due(nowSeconds)) {
        return;
    }
    lastPoll_ = nowSeconds;
    JobSystem& jobs = JobSystem::getInstance();
    if (!jobs.isRunning()) {
        scanNow(roots, paths);
        return;
    }
    // Фоновая задача: главный поток её не крадёт, кадр не ждёт диска и компиляции.
    job_ = jobs.submitBackground([shared = shared_, roots, paths] { scan(*shared, roots, paths); }, JobPriority::Low);
}

void ScriptWatcher::scanNow(const std::vector<std::string>& roots, const std::vector<std::string>& paths) {
    scan(*shared_, roots, paths);
}

std::vector<ScriptWatcher::Change> ScriptWatcher::drain() {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    std::vector<Change> changes;
    changes.swap(shared_->ready);
    return changes;
}

std::size_t ScriptWatcher::scans() const {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->scans;
}
