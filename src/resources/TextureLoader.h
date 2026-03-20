#pragma once

#include "ResourceTypes.h"
#include <string>

class IRenderAdapter;

// Загрузчик текстур через stb_image
class TextureLoader {
public:
    static bool load(const std::string& path, TextureData& textureData, IRenderAdapter* renderer);

private:
    static bool uploadToGPU(TextureData& textureData, IRenderAdapter* renderer);
};
