#pragma once

#include "ecs/System.h"
#include "math/MathTypes.h"

#include <cstddef>

class IRenderAdapter;
class World;
struct SubMesh;
struct MeshData;
struct ShaderData;
struct MeshRenderer;

class RenderSystem : public RenderSystemBase {
public:
    explicit RenderSystem(IRenderAdapter& renderer);

    void render(World& world) override;
    std::size_t getLastDrawnMeshCount() const { return lastDrawnMeshCount_; }

private:
    void renderSubMesh(const SubMesh& subMesh, const ShaderData& shaderData, const MeshRenderer& meshRenderer);
    void setupMatrices(World& world, unsigned int shaderProgram, const Mat4& modelMatrix);
    void setupLighting(unsigned int shaderProgram);
    
    IRenderAdapter& renderer_;
    std::size_t lastDrawnMeshCount_ = 0;
};
