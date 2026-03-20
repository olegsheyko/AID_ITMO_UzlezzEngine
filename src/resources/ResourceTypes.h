#pragma once

#include "math/MathTypes.h"
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

// Структура для хранения данных вершины
struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec2 texCoord;
};

// Материал меша
struct Material {
    std::string name;
    std::string diffuseTexturePath;
    std::string normalTexturePath;
    std::string metallicTexturePath;
    std::string roughnessTexturePath;
    std::string aoTexturePath;
    std::string heightTexturePath;
    Vec3 diffuseColor{1.0f, 1.0f, 1.0f};
};

// Подмеш (часть модели с одним материалом)
struct SubMesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    Material material;
    
    // GPU хендлы
    uint32_t vao = 0;
    uint32_t vbo = 0;
    uint32_t ebo = 0;
    uint32_t indexCount = 0;
};

// Данные меша (может содержать несколько подмешей)
struct MeshData {
    std::vector<SubMesh> subMeshes;
    
    // Для обратной совместимости - если модель простая (один меш)
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t vao = 0;
    uint32_t vbo = 0;
    uint32_t ebo = 0;
    uint32_t indexCount = 0;
    
    bool hasMultipleMaterials() const {
        return !subMeshes.empty();
    }
};

// Данные текстуры
struct TextureData {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = nullptr;
    
    // GPU хендл
    uint32_t textureId = 0;
    
    ~TextureData() {
        if (pixels) {
            // stb_image использует malloc, освобождаем через free
            free(pixels);
            pixels = nullptr;
        }
    }
};

// Данные шейдерной программы
struct ShaderData {
    std::string vertexPath;
    std::string fragmentPath;
    std::string vertexSource;
    std::string fragmentSource;
    
    // GPU хендл
    uint32_t programId = 0;
};
