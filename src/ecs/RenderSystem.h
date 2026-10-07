#pragma once

#include "ecs/System.h"
#include "math/MathTypes.h"

#include "ecs/Entity.h"

#include <cstddef>
#include <vector>

class IRenderAdapter;
class World;
struct SubMesh;
struct MeshData;
struct ShaderData;
struct MeshRenderer;
struct Material;

class RenderSystem : public RenderSystemBase {
public:
    explicit RenderSystem(IRenderAdapter& renderer);

    void render(World& world) override;
    // Только геометрия сущностей программой маски — для контура выделения в редакторе.
    void renderMask(World& world, const std::vector<Entity>& entities, unsigned int maskProgram);
    std::size_t getLastDrawnMeshCount() const { return lastDrawnMeshCount_; }

private:
    bool drawEntity(World& world, Entity entity, const MeshRenderer& meshRenderer, unsigned int maskProgram);
    void bindMaterial(const Material& material, const ShaderData& shaderData, const MeshRenderer& meshRenderer);
    void renderSubMesh(const SubMesh& subMesh, const ShaderData& shaderData, const MeshRenderer& meshRenderer);
    void setupMatrices(World& world, unsigned int shaderProgram, const Mat4& modelMatrix);
    void setupLighting(unsigned int shaderProgram);
    
    IRenderAdapter& renderer_;
    std::size_t lastDrawnMeshCount_ = 0;
};
