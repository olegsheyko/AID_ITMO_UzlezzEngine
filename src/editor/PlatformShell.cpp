#include "editor/PlatformShell.h"

#include "core/Logger.h"

#include <filesystem>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace PlatformShell {
namespace {
#if !defined(_WIN32)
bool spawn(const std::vector<std::string>& arguments) {
    std::vector<char*> argv;
    for (const std::string& argument : arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    pid_t pid = 0;
    const int result = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ);
    if (result != 0) {
        LOG_WARN("PlatformShell: failed to run " + arguments.front());
        return false;
    }
    // open/xdg-open сразу отдают управление; дожидаемся, чтобы не оставлять зомби-процесс.
    int status = 0;
    waitpid(pid, &status, 0);
    return true;
}
#endif
}

std::string absolutePath(const std::string& path) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return error ? path : absolute.lexically_normal().string();
}

std::string fileManagerName() {
#if defined(__APPLE__)
    return "Finder";
#elif defined(_WIN32)
    return "Explorer";
#else
    return "File Manager";
#endif
}

bool openFile(const std::string& path) {
    const std::string absolute = absolutePath(path);
#if defined(_WIN32)
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteA(nullptr, "open", absolute.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
#elif defined(__APPLE__)
    return spawn({"open", absolute});
#else
    return spawn({"xdg-open", absolute});
#endif
}

bool revealInFileManager(const std::string& path) {
    const std::string absolute = absolutePath(path);
#if defined(_WIN32)
    const std::string arguments = "/select,\"" + absolute + "\"";
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteA(nullptr, "open", "explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL));
    return result > 32;
#elif defined(__APPLE__)
    return spawn({"open", "-R", absolute});
#else
    std::error_code error;
    const std::string folder = std::filesystem::is_directory(absolute, error)
        ? absolute : std::filesystem::path(absolute).parent_path().string();
    return spawn({"xdg-open", folder});
#endif
}
} // namespace PlatformShell
