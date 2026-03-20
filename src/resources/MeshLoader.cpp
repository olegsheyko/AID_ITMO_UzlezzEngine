#include "MeshLoader.h"
#include "render/IRenderAdapter.h"
#include "core/Logger.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace {
std::string buildAssetPath(const std::string& directory, const aiString& texturePath) {
    std::string path = texturePath.C_Str();
    if (path.empty() || path[0] == '*') {
        return {};
    }

    if (path.find(':') != std::string::npos || path.rfind('/', 0) == 0 || path.rfind('\\', 0) == 0) {
        return path;
    }

    return directory + "/" + path;
}
}

bool MeshLoader::load(const std::string& path, MeshData& meshData, IRenderAdapter* renderer) {
    LOG_INFO("Loading mesh from: " + path);

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path,
        aiProcess_Triangulate |
            aiProcess_GenNormals |
            aiProcess_CalcTangentSpace |
            aiProcess_JoinIdenticalVertices);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || scene->mRootNode == nullptr) {
        LOG_ERROR("Assimp error: " + std::string(importer.GetErrorString()));
        return false;
    }

    meshData = MeshData{};
    processNode(scene->mRootNode, scene, meshData, getDirectory(path));

    if (meshData.subMeshes.empty()) {
        LOG_ERROR("No submeshes were extracted from: " + path);
        return false;
    }

    if (meshData.subMeshes.size() == 1) {
        meshData.vertices = meshData.subMeshes.front().vertices;
        meshData.indices = meshData.subMeshes.front().indices;
    }

    LOG_INFO("Processed " + std::to_string(meshData.subMeshes.size()) + " submeshes");
    return uploadToGPU(meshData, renderer);
}

void MeshLoader::processNode(aiNode* node, const aiScene* scene, MeshData& meshData, const std::string& directory) {
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        meshData.subMeshes.push_back(processMesh(mesh, scene, directory));
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        processNode(node->mChildren[i], scene, meshData, directory);
    }
}

SubMesh MeshLoader::processMesh(aiMesh* mesh, const aiScene* scene, const std::string& directory) {
    SubMesh subMesh;

    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        Vertex vertex{};
        vertex.position = {mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z};

        if (mesh->HasNormals()) {
            vertex.normal = {mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z};
        }

        if (mesh->mTextureCoords[0] != nullptr) {
            vertex.texCoord = {mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y};
        }

        subMesh.vertices.push_back(vertex);
    }

    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const aiFace& face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; ++j) {
            subMesh.indices.push_back(face.mIndices[j]);
        }
    }

    if (mesh->mMaterialIndex < scene->mNumMaterials) {
        subMesh.material = processMaterial(scene->mMaterials[mesh->mMaterialIndex], directory);
    }

    return subMesh;
}

Material MeshLoader::processMaterial(aiMaterial* material, const std::string& directory) {
    Material mat;

    aiString name;
    if (material->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
        mat.name = name.C_Str();
    }

    aiColor3D color(1.0f, 1.0f, 1.0f);
    if (material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
        mat.diffuseColor = {color.r, color.g, color.b};
    }

    auto readTexture = [&](aiTextureType type, std::string& outPath) {
        aiString texturePath;
        if (material->GetTexture(type, 0, &texturePath) == AI_SUCCESS) {
            outPath = buildAssetPath(directory, texturePath);
        }
    };

    readTexture(aiTextureType_DIFFUSE, mat.diffuseTexturePath);
    readTexture(aiTextureType_NORMALS, mat.normalTexturePath);
    readTexture(aiTextureType_METALNESS, mat.metallicTexturePath);
    readTexture(aiTextureType_DIFFUSE_ROUGHNESS, mat.roughnessTexturePath);
    readTexture(aiTextureType_AMBIENT_OCCLUSION, mat.aoTexturePath);
    readTexture(aiTextureType_HEIGHT, mat.heightTexturePath);

    return mat;
}

bool MeshLoader::uploadToGPU(MeshData& meshData, IRenderAdapter* renderer) {
    if (renderer == nullptr) {
        return false;
    }

    for (auto& subMesh : meshData.subMeshes) {
        if (!uploadSubMeshToGPU(subMesh, renderer)) {
            return false;
        }
    }

    if (meshData.subMeshes.size() == 1) {
        const SubMesh& subMesh = meshData.subMeshes.front();
        meshData.vao = subMesh.vao;
        meshData.vbo = subMesh.vbo;
        meshData.ebo = subMesh.ebo;
        meshData.indexCount = subMesh.indexCount;
    }

    LOG_INFO("Mesh uploaded to GPU successfully");
    return true;
}

bool MeshLoader::uploadSubMeshToGPU(SubMesh& subMesh, IRenderAdapter* renderer) {
    const bool uploaded = renderer->uploadMesh(
        subMesh.vertices.data(),
        sizeof(Vertex),
        subMesh.vertices.size(),
        subMesh.indices.data(),
        subMesh.indices.size(),
        subMesh.vao,
        subMesh.vbo,
        subMesh.ebo);

    if (uploaded) {
        subMesh.indexCount = static_cast<uint32_t>(subMesh.indices.size());
    }

    return uploaded;
}

std::string MeshLoader::getDirectory(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return ".";
    }

    return path.substr(0, slash);
}
