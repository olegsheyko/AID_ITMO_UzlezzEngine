#pragma once

#include "resources/Resource.h"
#include "resources/ResourceTypes.h"

#include <memory>
#include <string>
#include <unordered_map>

// Превью текстур для Content Browser, инспектора и выбора текстуры.
// Грузит через job system с низким приоритетом: текстуры сцены всегда обгоняют превью.
class ThumbnailCache {
public:
    static ThumbnailCache& instance();

    // Готовая текстура или nullptr, пока грузится или не смогла загрузиться.
    const TextureData* texture(const std::string& path);
    bool isLoading(const std::string& path) const;
    bool isFailed(const std::string& path) const;
    // Забывает неудачные попытки, чтобы после Refresh файл попробовали снова.
    void forgetFailures();

private:
    std::unordered_map<std::string, std::shared_ptr<Resource<TextureData>>> entries_;
};
