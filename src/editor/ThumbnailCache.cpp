#include "editor/ThumbnailCache.h"

#include "resources/ResourceManager.h"

ThumbnailCache& ThumbnailCache::instance() {
    static ThumbnailCache cache;
    return cache;
}

const TextureData* ThumbnailCache::texture(const std::string& path) {
    auto [it, inserted] = entries_.try_emplace(path);
    if (inserted) {
        it->second = ResourceManager::getInstance().loadTextureAsync(path, JobPriority::Low);
    }
    const auto& resource = it->second;
    if (resource && resource->isLoaded() && resource->getData()->textureId != 0) {
        return resource->getData();
    }
    return nullptr;
}

bool ThumbnailCache::isLoading(const std::string& path) const {
    auto it = entries_.find(path);
    return it != entries_.end() && it->second && it->second->isPending();
}

bool ThumbnailCache::isFailed(const std::string& path) const {
    auto it = entries_.find(path);
    return it != entries_.end() && (!it->second || it->second->isFailed());
}

void ThumbnailCache::forgetFailures() {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (!it->second || it->second->isFailed()) {
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}
