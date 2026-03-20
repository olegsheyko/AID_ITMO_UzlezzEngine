#pragma once

#include "ResourceTypes.h"
#include <string>

// Forward declarations
struct aiNode;
struct aiMesh;
struct aiMaterial;
struct aiScene;
class IRenderAdapter;

// Загрузчик мешей через Assimp
class MeshLoader {
public:
    static bool load(const std::string& path, MeshData& meshData, IRenderAdapter* renderer);

private:
    static void processNode(aiNode* node, const aiScene* scene, MeshData& meshData, const std::string& directory);
    static SubMesh processMesh(aiMesh* mesh, const aiScene* scene, const std::string& directory);
    static Material processMaterial(aiMaterial* material, const std::string& directory);
    static bool uploadToGPU(MeshData& meshData, IRenderAdapter* renderer);
    static bool uploadSubMeshToGPU(SubMesh& subMesh);
    static std::string getDirectory(const std::string& path);
};
