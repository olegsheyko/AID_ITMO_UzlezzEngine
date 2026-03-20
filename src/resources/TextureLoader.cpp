#include "TextureLoader.h"
#include "render/IRenderAdapter.h"
#include "core/Logger.h"

#include <stb_image.h>
#include <glad/glad.h>

bool TextureLoader::load(const std::string& path, TextureData& textureData, IRenderAdapter* renderer) {
    LOG_INFO("Loading texture from disk: " + path);
    
    // Проверяем существование файла
    FILE* testFile = fopen(path.c_str(), "rb");
    if (!testFile) {
        LOG_ERROR("Texture file does not exist or cannot be opened: " + path);
        return false;
    }
    fclose(testFile);
    LOG_INFO("File exists and can be opened");
    
    // НЕ инвертируем текстуры
    stbi_set_flip_vertically_on_load(false);
    
    // Загружаем изображение НАПРЯМУЮ с диска
    int width, height, channels;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 0);

    if (!pixels) {
        LOG_ERROR("stb_image failed to load texture: " + path);
        return false;
    }
    
    LOG_INFO("stb_image loaded: " + std::to_string(width) + "x" + std::to_string(height));
    
    // Проверяем что это не placeholder от Assimp
    if (width <= 2 || height <= 2) {
        LOG_ERROR("Loaded placeholder texture (2x2) - ignoring: " + path);
        stbi_image_free(pixels);
        return false;
    }
    
    textureData.width = width;
    textureData.height = height;
    textureData.channels = channels;
    textureData.pixels = pixels;

    LOG_INFO("Real texture loaded from disk: " + std::to_string(width) + "x" + 
             std::to_string(height) + ", channels=" + std::to_string(channels));

    bool result = uploadToGPU(textureData, renderer);
    
    // Освобождаем память после загрузки на GPU
    stbi_image_free(textureData.pixels);
    textureData.pixels = nullptr;
    
    return result;
}

bool TextureLoader::uploadToGPU(TextureData& textureData, IRenderAdapter* renderer) {
    // Создаем текстуру
    glGenTextures(1, &textureData.textureId);
    glBindTexture(GL_TEXTURE_2D, textureData.textureId);

    // Определяем формат текстуры
    GLenum internalFormat = GL_RGB;
    GLenum dataFormat = GL_RGB;
    
    if (textureData.channels == 1) {
        internalFormat = GL_RED;
        dataFormat = GL_RED;
    } else if (textureData.channels == 3) {
        internalFormat = GL_RGB;
        dataFormat = GL_RGB;
    } else if (textureData.channels == 4) {
        internalFormat = GL_RGBA;
        dataFormat = GL_RGBA;
    }

    // Загружаем данные на GPU
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, textureData.width, textureData.height, 
                 0, dataFormat, GL_UNSIGNED_BYTE, textureData.pixels);
    
    // Генерируем мипмапы
    glGenerateMipmap(GL_TEXTURE_2D);

    // Параметры фильтрации
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);

    LOG_INFO("Texture uploaded to GPU: ID=" + std::to_string(textureData.textureId));
    return true;
}
