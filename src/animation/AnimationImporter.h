#pragma once
struct aiScene;
struct MeshData;
// Called while the Assimp scene is alive; copies all data into engine-owned storage.
bool importAnimation(const aiScene& scene, MeshData& mesh);
