#pragma once

#include "ecs/System.h"

class IRenderAdapter;
class World;
struct MeshData;
struct ShaderData;
struct MeshRenderer;

class RenderSystem : public RenderSystemBase {
public:
    explicit RenderSystem(IRenderAdapter& renderer);

    void render(World& world) override;

private:
    void renderSubMeshes(const MeshData* meshData, const ShaderData* shaderData);
    void renderSubMeshesWithOverride(const MeshData* meshData, const ShaderData* shaderData,
                                     const MeshRenderer& meshRenderer);
    void renderSimpleMesh(const MeshData* meshData, const ShaderData* shaderData, 
                         const MeshRenderer& meshRenderer);
    
    IRenderAdapter& renderer_;
};
