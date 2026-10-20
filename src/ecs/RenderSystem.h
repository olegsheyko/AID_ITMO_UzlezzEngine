#pragma once

#include "ecs/System.h"
#include "math/MathTypes.h"

#include "ecs/Entity.h"
#include "ecs/Visibility.h"

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
    bool cullingEnabled = true;
    bool sortingEnabled = true;
    bool depthSortingEnabled = true;
    struct Stats {
        std::size_t registered=0, visible=0, submitted=0, updates=0;
        std::size_t shaderChanges=0, textureChanges=0, depthInversions=0;
        double prepareMs=0, submitMs=0;
    };
    const Stats& stats() const { return stats_; }
    const std::vector<Visibility::Handle>& visibleSet() const { return visible_; }
    explicit RenderSystem(IRenderAdapter& renderer);

    void render(World& world) override;
    // Только геометрия сущностей программой маски — для контура выделения в редакторе.
    void renderMask(World& world, const std::vector<Entity>& entities, unsigned int maskProgram);
    std::size_t getLastDrawnMeshCount() const { return lastDrawnMeshCount_; }

private:
    struct Submission {
        Entity entity;
        Mat4 model;
        unsigned int shader;
        std::uintptr_t mesh, texture;
        float depth;
    };
    void prepare(World& world);
    void submit(World& world);
    void selectCamera(const World& world);
    bool drawEntity(World& world, Entity entity, const MeshRenderer& meshRenderer, unsigned int maskProgram);
    void bindMaterial(const Material& material, const ShaderData& shaderData, const MeshRenderer& meshRenderer);
    void renderSubMesh(const SubMesh& subMesh, const ShaderData& shaderData, const MeshRenderer& meshRenderer);
    void setupLighting(unsigned int shaderProgram);
    
    IRenderAdapter& renderer_;
    std::size_t lastDrawnMeshCount_ = 0;
    Stats stats_;
    std::vector<Visibility::Handle> visible_;
    std::vector<Submission> packet_;
    Mat4 view_=Mat4::identity(), projection_=Mat4::identity();
    const Mat4* submittedModel_=nullptr;
    unsigned int boundProgram_=~0u, boundTexture_=~0u;
};
