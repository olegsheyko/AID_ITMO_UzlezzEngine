// tinyobjloader - заглушка с реализацией
// Полную версию можно скачать с https://github.com/tinyobjloader/tinyobjloader

#ifndef TINY_OBJ_LOADER_H
#define TINY_OBJ_LOADER_H

#include <vector>
#include <string>

namespace tinyobj {

struct index_t {
    int vertex_index = -1;
    int normal_index = -1;
    int texcoord_index = -1;
};

struct mesh_t {
    std::vector<index_t> indices;
    std::vector<unsigned char> num_face_vertices;
    std::vector<int> material_ids;
};

struct shape_t {
    std::string name;
    mesh_t mesh;
};

struct material_t {
    std::string name;
};

struct attrib_t {
    std::vector<float> vertices;
    std::vector<float> normals;
    std::vector<float> texcoords;
};

// Объявление функции
bool LoadObj(attrib_t* attrib, std::vector<shape_t>* shapes, 
             std::vector<material_t>* materials, std::string* warn, 
             std::string* err, const char* filename);

} // namespace tinyobj

#endif // TINY_OBJ_LOADER_H

#ifdef TINYOBJLOADER_IMPLEMENTATION

namespace tinyobj {

// Реализация функции загрузки
bool LoadObj(attrib_t* attrib, std::vector<shape_t>* shapes, 
             std::vector<material_t>* materials, std::string* warn, 
             std::string* err, const char* filename) {
    (void)filename;
    (void)materials;
    (void)warn;
    (void)err;
    
    // Заглушка - создаём простой треугольник
    attrib->vertices = {
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
         0.0f,  0.5f, 0.0f
    };
    
    attrib->normals = {
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f
    };
    
    attrib->texcoords = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.5f, 1.0f
    };
    
    shape_t shape;
    shape.name = "triangle";
    
    for (int i = 0; i < 3; i++) {
        index_t idx;
        idx.vertex_index = i;
        idx.normal_index = i;
        idx.texcoord_index = i;
        shape.mesh.indices.push_back(idx);
    }
    
    shapes->push_back(shape);
    
    return true;
}

} // namespace tinyobj

#endif // TINYOBJLOADER_IMPLEMENTATION
