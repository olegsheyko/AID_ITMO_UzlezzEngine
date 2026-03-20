#include "HotReload.h"
#include "core/Logger.h"

#include <vector>

#ifdef _WIN32
#include <sys/types.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#endif

HotReload& HotReload::getInstance() {
    static HotReload instance;
    return instance;
}

std::time_t HotReload::getFileModTime(const std::string& path) {
#ifdef _WIN32
    struct _stat fileInfo;
    if (_stat(path.c_str(), &fileInfo) == 0) {
        return fileInfo.st_mtime;
    }
#else
    struct stat fileInfo;
    if (stat(path.c_str(), &fileInfo) == 0) {
        return fileInfo.st_mtime;
    }
#endif
    return 0;
}

void HotReload::watchFile(const std::string& path) {
    std::time_t modTime = getFileModTime(path);
    if (modTime != 0) {
        FileInfo info;
        info.lastWriteTime = modTime;
        watchedFiles_[path] = info;
        LOG_INFO("HotReload: watching file " + path);
    }
}

bool HotReload::update() {
    changedFiles_.clear();
    
    for (auto& pair : watchedFiles_) {
        const std::string& path = pair.first;
        FileInfo& info = pair.second;
        
        std::time_t currentTime = getFileModTime(path);
        if (currentTime != 0 && currentTime != info.lastWriteTime) {
            info.lastWriteTime = currentTime;
            LOG_INFO("HotReload: file changed " + path);
            changedFiles_.push_back(path);
        }
    }
    
    return !changedFiles_.empty();
}
