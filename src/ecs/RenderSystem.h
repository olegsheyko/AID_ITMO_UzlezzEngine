#pragma once

#include "ecs/System.h"
#include "math/MathTypes.h"

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

private:
    void renderSubMesh(const SubMesh& subMesh, const ShaderData& shaderData, const MeshRenderer& meshRenderer);
    void setupMatrices(World& world, unsigned int shaderProgram, const Mat4& modelMatrix);
    void setupLighting(unsigned int shaderProgram);
    
    IRenderAdapter& renderer_;
};
