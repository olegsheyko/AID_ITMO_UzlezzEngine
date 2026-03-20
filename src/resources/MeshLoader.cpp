#include "MeshLoader.h"
#include "render/IRenderAdapter.h"
#include "core/Logger.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <glad/glad.h>

bool MeshLoader::load(const std::string& path, MeshData& meshData, IRenderAdapter* renderer) {
    LOG_INFO("Loading mesh from: " + path);
    
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, 
        aiProcess_Triangulate | 
        aiProcess_GenNormals |
        aiProcess_CalcTangentSpace);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        LOG_ERROR("Assimp error: " + std::string(importer.GetErrorString()));
        return false;
    }

    LOG_INFO("Assimp: Loaded " + std::to_string(scene->mNumMeshes) + " meshes, " + 
             std::to_string(scene->mNumMaterials) + " materials");

    std::string directory = getDirectory(path);
    processNode(scene->mRootNode, scene, meshData, directory);

    LOG_INFO("Processed " + std::to_string(meshData.subMeshes.size()) + " submeshes");

    return uploadToGPU(meshData, renderer);
}

void MeshLoader::processNode(aiNode* node, const aiScene* scene, MeshData& meshData, const std::string& directory) {
    // Обрабатываем все меши в узле
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        SubMesh subMesh = processMesh(mesh, scene, directory);
        meshData.subMeshes.push_back(subMesh);
    }
    
    // Рекурсивно обрабатываем дочерние узлы
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        processNode(node->mChildren[i], scene, meshData, directory);
    }
}

SubMesh MeshLoader::processMesh(aiMesh* mesh, const aiScene* scene, const std::string& directory) {
    SubMesh subMesh;
    
    bool hasTexCoords = mesh->mTextureCoords[0] != nullptr;
    LOG_INFO("Processing submesh: " + std::to_string(mesh->mNumVertices) + " vertices, hasTexCoords=" + 
             (hasTexCoords ? "true" : "false"));
    
    // Обрабатываем вершины
    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        Vertex vertex{};

        // Позиция
        vertex.position.x = mesh->mVertices[i].x;
        vertex.position.y = mesh->mVertices[i].y;
        vertex.position.z = mesh->mVertices[i].z;

        // Нормаль
        if (mesh->HasNormals()) {
            vertex.normal.x = mesh->mNormals[i].x;
            vertex.normal.y = mesh->mNormals[i].y;
            vertex.normal.z = mesh->mNormals[i].z;
        } else {
            vertex.normal = {0.0f, 1.0f, 0.0f};
        }

        // Текстурные координаты
        if (mesh->mTextureCoords[0]) {
            vertex.texCoord.x = mesh->mTextureCoords[0][i].x;
            vertex.texCoord.y = mesh->mTextureCoords[0][i].y;
        } else {
            vertex.texCoord = {0.0f, 0.0f};
        }

        subMesh.vertices.push_back(vertex);
    }

    // Обрабатываем индексы
    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            subMesh.indices.push_back(face.mIndices[j]);
        }
    }
    
    // Обрабатываем материал
    if (mesh->mMaterialIndex >= 0) {
        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
        subMesh.material = processMaterial(material, directory);
    }
    
    return subMesh;
}

Material MeshLoader::processMaterial(aiMaterial* material, const std::string& directory) {
    Material mat;
    
    // Получаем имя материала
    aiString name;
    material->Get(AI_MATKEY_NAME, name);
    mat.name = name.C_Str();
    
    LOG_INFO("Processing material: " + mat.name);
    
    // НЕ загружаем текстуры из Assimp - они встроенные 2x2 placeholder'ы
    // Текстуры будут загружаться из MeshRenderer компонента
    
    // Получаем diffuse цвет
    aiColor3D color;
    if (material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
        mat.diffuseColor = {color.r, color.g, color.b};
        LOG_INFO("  Diffuse color: (" + std::to_string(color.r) + ", " + 
                 std::to_string(color.g) + ", " + std::to_string(color.b) + ")");
    }
    
    return mat;
}

std::string MeshLoader::getDirectory(const std::string& path) {
    size_t lastSlash = path.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        return path.substr(0, lastSlash);
    }
    return ".";
}

bool MeshLoader::uploadToGPU(MeshData& meshData, IRenderAdapter* renderer) {
    // Загружаем все submeshes на GPU
    for (auto& subMesh : meshData.subMeshes) {
        if (!uploadSubMeshToGPU(subMesh)) {
            return false;
        }
    }
    
    LOG_INFO("All submeshes uploaded to GPU successfully");
    return true;
}

bool MeshLoader::uploadSubMeshToGPU(SubMesh& subMesh) {
    // Создаём VAO, VBO, EBO
    glGenVertexArrays(1, &subMesh.vao);
    glGenBuffers(1, &subMesh.vbo);
    glGenBuffers(1, &subMesh.ebo);

    glBindVertexArray(subMesh.vao);

    // VBO
    glBindBuffer(GL_ARRAY_BUFFER, subMesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, subMesh.vertices.size() * sizeof(Vertex), 
                 subMesh.vertices.data(), GL_STATIC_DRAW);

    // EBO
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, subMesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, subMesh.indices.size() * sizeof(uint32_t), 
                 subMesh.indices.data(), GL_STATIC_DRAW);

    // Атрибуты вершин
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(6 * sizeof(float)));

    glBindVertexArray(0);

    return true;
}
