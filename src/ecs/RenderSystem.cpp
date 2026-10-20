#include "ecs/RenderSystem.h"
#include <tracy/Tracy.hpp>
#include "ecs/Components.h"
#include "ecs/World.h"
#include "math/MathTypes.h"
#include "math/CameraMath.h"
#include "render/IRenderAdapter.h"
#include "resources/ResourceManager.h"

#include <cstddef>
#include <unordered_set>
#include "ecs/SceneVisibility.h"
#include <chrono>
#include <tuple>
#include <algorithm>

RenderSystem::RenderSystem(IRenderAdapter& renderer)
    : renderer_(renderer) {
}

void RenderSystem::render(World& world) {
    ZoneScopedN("Scene visibility and submission");
    stats_={};
    lastDrawnMeshCount_=0;
    auto start=std::chrono::steady_clock::now();
    prepare(world);
    auto prepared=std::chrono::steady_clock::now();
    submit(world);
    stats_.prepareMs=std::chrono::duration<double,std::milli>(prepared-start).count();
    stats_.submitMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-prepared).count();
}

void RenderSystem::prepare(World& world) {
    ZoneScopedN("Visible set and sorted packet");
    selectCamera(world);
    stats_.updates=SceneVisibility::synchronize(world);
    stats_.registered=world.visibility.size();
    visible_=world.visibility.query(Math::multiply(projection_,view_),cullingEnabled);
    stats_.visible=visible_.size();
    packet_.clear();
    const World& read=world;
    for (auto h:visible_) {
        const auto& r=*world.visibility.get(h);
        const auto& mr=read.getComponent<MeshRenderer>(r.entity);
        const unsigned int shader=mr.cachedShader->isLoaded() ? mr.cachedShader->getData()->programId:0;
        const Vec3 center={(r.bounds.min.x+r.bounds.max.x)*.5f,(r.bounds.min.y+r.bounds.max.y)*.5f,(r.bounds.min.z+r.bounds.max.z)*.5f};
        packet_.push_back({r.entity,r.model,shader,reinterpret_cast<std::uintptr_t>(mr.cachedMesh.get()),
            reinterpret_cast<std::uintptr_t>(mr.cachedBaseColorTexture.get()),-Visibility::point(view_,center).z});
    }
    // Stable unsorted order makes A/B runs independent of hash table iteration.
    std::sort(packet_.begin(),packet_.end(),[](const auto& a,const auto& b){return a.entity<b.entity;});
    if (sortingEnabled) std::sort(packet_.begin(),packet_.end(),[this](const auto& a,const auto& b) {
        if (!depthSortingEnabled)
            return std::tie(a.shader,a.mesh,a.texture,a.entity)<std::tie(b.shader,b.mesh,b.texture,b.entity);
        return std::tie(a.shader,a.mesh,a.texture,a.depth,a.entity)<std::tie(b.shader,b.mesh,b.texture,b.depth,b.entity);
    });
    for (std::size_t i=1;i<packet_.size();++i) {
        const auto& a=packet_[i-1]; const auto& b=packet_[i];
        if (std::tie(a.shader,a.mesh,a.texture)==std::tie(b.shader,b.mesh,b.texture) && a.depth>b.depth)
            ++stats_.depthInversions;
    }
}

void RenderSystem::submit(World& world) {
    ZoneScopedN("Submission packet");
    boundProgram_=boundTexture_=~0u;
    const World& read=world;
    for (const auto& item:packet_) {
        submittedModel_=&item.model;
        if (drawEntity(world,item.entity,read.getComponent<MeshRenderer>(item.entity),0)) ++lastDrawnMeshCount_;
    }
    submittedModel_=nullptr;
    renderer_.useShaderProgram(0);
    renderer_.bindTexture2D(0,0);
    stats_.submitted=lastDrawnMeshCount_;
}

void RenderSystem::renderMask(World& world, const std::vector<Entity>& entities, unsigned int maskProgram) {
    if (maskProgram == 0) {
        return;
    }
    selectCamera(world);
    boundProgram_=boundTexture_=~0u;
    for (Entity entity : entities) {
        if (world.isAlive(entity) && world.hasComponent<Transform>(entity) && world.hasComponent<MeshRenderer>(entity)) {
            drawEntity(world, entity, static_cast<const World&>(world).getComponent<MeshRenderer>(entity), maskProgram);
        }
    }
}

bool RenderSystem::drawEntity(World& world, Entity entity, const MeshRenderer& meshRenderer, unsigned int maskProgram) {
    if (!meshRenderer.visible || !meshRenderer.cachedMesh || !meshRenderer.cachedShader) {
        return false;
    }

    if (!meshRenderer.cachedShader->isLoaded()) {
        return false;
    }

    const bool placeholder = !meshRenderer.cachedMesh->isLoaded();
    // This tiny procedural resource performs no file I/O and is cached once.
    auto mesh = placeholder ? ResourceManager::getInstance().loadMesh("primitive:cube") : meshRenderer.cachedMesh;
    if (!mesh || !mesh->isLoaded()) return false;
    const MeshData* meshData = mesh->getData();
    const ShaderData* shaderData = meshRenderer.cachedShader->getData();
    if (meshData == nullptr || shaderData == nullptr || shaderData->programId == 0) {
        return false;
    }

    // Маска рисует ту же геометрию своей программой: без материалов и света.
    const bool mask = maskProgram != 0;
    const unsigned int program = mask ? maskProgram : shaderData->programId;

    std::unordered_set<Entity> visited;
    Mat4 modelMatrix = submittedModel_ ? *submittedModel_ : SceneVisibility::worldMatrix(world, entity, visited);
    if (!submittedModel_ && meshRenderer.yUpSource) {
        modelMatrix = Math::multiply(modelMatrix, Math::rotationX(1.5707963f));
    }

    if (boundProgram_!=program) {
        renderer_.useShaderProgram(program);
        boundProgram_=program;
        ++stats_.shaderChanges;
        renderer_.setMatrix4(program,"view",view_);
        renderer_.setMatrix4(program,"projection",projection_);
        if (!mask) setupLighting(program);
    }
    renderer_.setMatrix4(program,"model",modelMatrix);

    const AnimationPose* pose = nullptr;
    if (world.hasComponent<Animator>(entity)) {
        const auto& animator = static_cast<const World&>(world).getComponent<Animator>(entity);
        const auto& candidate = animator.pose;
        if (animator.evaluatedMesh == meshData && candidate.globals.size() == meshData->skeleton.nodes.size()
            && candidate.palettes.size() == meshData->subMeshes.size()) pose = &candidate;
    }
    renderer_.setInt(program, "useSkinning", 0);
    renderer_.setMatrix4(program, "meshNodeTransform", Mat4::identity());

    if (placeholder) {
        if (!mask) {
            renderer_.bindTexture2D(ResourceManager::getInstance().placeholderTextureId(), 0);
            boundTexture_=ResourceManager::getInstance().placeholderTextureId();
            ++stats_.textureChanges;
            renderer_.setInt(program, "baseColorTexture", 0);
            renderer_.setInt(program, "useBaseColorTexture", 1);
            renderer_.setVec3(program, "materialColor",
                meshRenderer.cachedMesh->isFailed() ? Vec3{1,.2f,.2f} : Vec3{1,1,1});
        }
        for (const auto& sub : meshData->subMeshes) renderer_.drawIndexed(sub.vao, sub.indexCount);
    } else if (!meshData->subMeshes.empty()) {
        for (size_t i = 0; i < meshData->subMeshes.size(); ++i) {
            const SubMesh& subMesh = meshData->subMeshes[i];
            if (!meshData->skeleton.nodes.empty()) {
                const auto& globals = pose ? pose->globals : meshData->skeleton.bindGlobals;
                renderer_.setMatrix4(program, "meshNodeTransform",
                    Math::multiply(meshData->skeleton.rootInverse, globals[subMesh.skeletonNode]));
            }
            const bool skinned = pose && !subMesh.bones.empty() && pose->palettes[i].size() == subMesh.bones.size();
            renderer_.setInt(program, "useSkinning", skinned ? 1 : 0);
            if (skinned) renderer_.setSkinMatrices(program, pose->palettes[i].data(), pose->palettes[i].size());
            if (mask) {
                renderer_.drawIndexed(subMesh.vao, subMesh.indexCount);
            } else {
                renderSubMesh(subMesh, *shaderData, meshRenderer);
            }
        }
    } else if (meshData->vao != 0 && meshData->indexCount > 0) {
        if (!mask) {
            bindMaterial(Material{}, *shaderData, meshRenderer);
        }
        renderer_.drawIndexed(meshData->vao, meshData->indexCount);
    }

    return true;
}

void RenderSystem::setupLighting(unsigned int shaderProgram) {
    renderer_.setVec3(shaderProgram, "light.direction", Vec3{-0.5f, 0.3f, -1.0f});
    renderer_.setVec3(shaderProgram, "light.color", Vec3{1.0f, 0.96f, 0.9f});
    renderer_.setFloat(shaderProgram, "light.ambientStrength", 0.35f);
    renderer_.setFloat(shaderProgram, "light.diffuseStrength", 0.95f);
}

void RenderSystem::bindMaterial(const Material& material, const ShaderData& shaderData, const MeshRenderer& meshRenderer) {
    std::shared_ptr<Resource<TextureData>> texture = meshRenderer.cachedBaseColorTexture;
    if (!texture) {
        texture = material.cachedDiffuseTexture;
    }

    unsigned int textureId = 0;
    if (texture && texture->isLoaded()) {
        textureId = texture->getData()->textureId;
    } else if (texture) {
        // Текстура назначена, но ещё грузится или не загрузилась — рисуем заглушку, а не цвет материала.
        textureId = ResourceManager::getInstance().placeholderTextureId();
    }
    const bool hasTexture = textureId != 0;
    // Сэмплер в шейдере есть всегда: без текстуры привязываем заглушку, иначе драйвер macOS
    // ругается на «unloadable» текстуру 0, хотя useBaseColorTexture = 0 её и не читает.
    const unsigned int effectiveTexture=hasTexture ? textureId : ResourceManager::getInstance().placeholderTextureId();
    if (boundTexture_!=effectiveTexture) {
        renderer_.bindTexture2D(effectiveTexture,0);
        boundTexture_=effectiveTexture;
        ++stats_.textureChanges;
    }
    renderer_.setInt(shaderData.programId, "baseColorTexture", 0);
    renderer_.setInt(shaderData.programId, "useBaseColorTexture", hasTexture ? 1 : 0);
    renderer_.setVec3(shaderData.programId, "materialColor", material.diffuseColor);
}

void RenderSystem::renderSubMesh(const SubMesh& subMesh, const ShaderData& shaderData, const MeshRenderer& meshRenderer) {
    bindMaterial(subMesh.material, shaderData, meshRenderer);
    renderer_.drawIndexed(subMesh.vao, subMesh.indexCount);
}

void RenderSystem::selectCamera(const World& world) {
    bool found=false;
    world.forEachRead<Camera>([&](Entity,const Camera& camera) {
        if (!found && camera.active) {view_=camera.viewMatrix; projection_=camera.projectionMatrix; found=true;}
    });
    if (!found) {
        view_=CameraMath::view({0,-5,0},0,0);
        int width=0,height=0;
        renderer_.getFramebufferSize(width,height);
        projection_=Math::perspective(.785398163f,height>0 ? float(width)/height:1.3333333f,.1f,100.f);
    }
}
