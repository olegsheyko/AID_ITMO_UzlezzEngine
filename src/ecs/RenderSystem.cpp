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

namespace {
Mat4 buildLocalMatrix(const Transform& transform) {
    return Math::composeTransform(transform.position, transform.rotation, transform.scale);
}

Mat4 buildWorldMatrix(World& world, Entity entity, std::unordered_set<Entity>& visited) {
    if (!world.isAlive(entity) || !world.hasComponent<Transform>(entity)) {
        return Mat4::identity();
    }

    if (!visited.insert(entity).second) {
        return buildLocalMatrix(world.getComponent<Transform>(entity));
    }

    Mat4 worldMatrix = buildLocalMatrix(world.getComponent<Transform>(entity));
    if (world.hasComponent<Hierarchy>(entity)) {
        const Hierarchy& hierarchy = world.getComponent<Hierarchy>(entity);
        if (hierarchy.parent != kInvalidEntity) {
            worldMatrix = Math::multiply(buildWorldMatrix(world, hierarchy.parent, visited), worldMatrix);
        }
    }

    visited.erase(entity);
    return worldMatrix;
}
}

RenderSystem::RenderSystem(IRenderAdapter& renderer)
    : renderer_(renderer) {
}

void RenderSystem::render(World& world) {
    ZoneScopedN("Scene render");
    lastDrawnMeshCount_ = 0;
    ZoneNamedN(submission, "Skin palette upload and scene draws", true);
    world.forEach<Transform, MeshRenderer>([this, &world](Entity entity, Transform&, MeshRenderer& meshRenderer) {
        if (drawEntity(world, entity, meshRenderer, 0)) {
            ++lastDrawnMeshCount_;
        }
    });
}

void RenderSystem::renderMask(World& world, const std::vector<Entity>& entities, unsigned int maskProgram) {
    if (maskProgram == 0) {
        return;
    }
    for (Entity entity : entities) {
        if (world.isAlive(entity) && world.hasComponent<Transform>(entity) && world.hasComponent<MeshRenderer>(entity)) {
            drawEntity(world, entity, world.getComponent<MeshRenderer>(entity), maskProgram);
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
    const Mat4 modelMatrix = buildWorldMatrix(world, entity, visited);

    renderer_.useShaderProgram(program);
    setupMatrices(world, program, modelMatrix);
    if (!mask) {
        setupLighting(program);
    }

    const AnimationPose* pose = nullptr;
    if (world.hasComponent<Animator>(entity)) {
        const auto& animator = world.getComponent<Animator>(entity);
        const auto& candidate = animator.pose;
        if (animator.evaluatedMesh == meshData && candidate.globals.size() == meshData->skeleton.nodes.size()
            && candidate.palettes.size() == meshData->subMeshes.size()) pose = &candidate;
    }
    renderer_.setInt(program, "useSkinning", 0);
    renderer_.setMatrix4(program, "meshNodeTransform", Mat4::identity());

    if (placeholder) {
        if (!mask) {
            renderer_.bindTexture2D(ResourceManager::getInstance().placeholderTextureId(), 0);
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

    renderer_.useShaderProgram(0);
    renderer_.bindTexture2D(0, 0);
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
    renderer_.bindTexture2D(textureId, 0);
    renderer_.setInt(shaderData.programId, "baseColorTexture", 0);
    renderer_.setInt(shaderData.programId, "useBaseColorTexture", hasTexture ? 1 : 0);
    renderer_.setVec3(shaderData.programId, "materialColor", material.diffuseColor);
}

void RenderSystem::renderSubMesh(const SubMesh& subMesh, const ShaderData& shaderData, const MeshRenderer& meshRenderer) {
    bindMaterial(subMesh.material, shaderData, meshRenderer);
    renderer_.drawIndexed(subMesh.vao, subMesh.indexCount);
}

void RenderSystem::setupMatrices(World& world, unsigned int shaderProgram, const Mat4& modelMatrix) {
    renderer_.setMatrix4(shaderProgram, "model", modelMatrix);

    Mat4 viewMatrix = Mat4::identity();
    Mat4 projectionMatrix = Mat4::identity();

    bool cameraFound = false;
    world.forEach<Transform, Camera>([&](Entity, Transform& transform, Camera& camera) {
        (void)transform;
        if (cameraFound || !camera.active) {
            return;
        }

        viewMatrix = camera.viewMatrix;
        projectionMatrix = camera.projectionMatrix;
        cameraFound = true;
    });

    if (!cameraFound) {
        viewMatrix = CameraMath::view(Vec3{0.0f, -5.0f, 0.0f}, 0.0f, 0.0f);
        int width = 0;
        int height = 0;
        renderer_.getFramebufferSize(width, height);
        const float aspect = (width > 0 && height > 0) ? static_cast<float>(width) / static_cast<float>(height) : (800.0f / 600.0f);
        projectionMatrix = Math::perspective(45.0f * 3.1415926f / 180.0f, aspect, 0.1f, 100.0f);
    }

    renderer_.setMatrix4(shaderProgram, "view", viewMatrix);
    renderer_.setMatrix4(shaderProgram, "projection", projectionMatrix);
}
