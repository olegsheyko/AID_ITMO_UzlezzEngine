#pragma once

#include "ResourceTypes.h"
#include <string>

class IRenderAdapter;

// Загрузчик текстур: stb_image и собственный декодер DDS.
// Загрузка режется на две фазы, чтобы тяжёлую часть можно было отдать воркеру.
class TextureLoader {
public:
    // Синхронно: decode, затем upload. Только главный поток.
    static bool load(const std::string& path, TextureData& textureData, IRenderAdapter* renderer);

    // CPU-фаза: прочитать и декодировать файл в textureData.pixels. Можно звать с любого потока.
    static bool decode(const std::string& path, TextureData& textureData);

    // GPU-фаза: создать текстуру из pixels и освободить их. Только главный поток — там GL-контекст.
    static bool upload(TextureData& textureData, IRenderAdapter* renderer, const std::string& debugName = {});

private:
    static bool uploadToGPU(TextureData& textureData, IRenderAdapter* renderer);
};
