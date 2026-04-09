#include "ecs/RenderSystem.h"
#include "ecs/Components.h"
#include "ecs/World.h"
#include "math/MathTypes.h"
#include "render/IRenderAdapter.h"
#include "resources/ResourceManager.h"

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
    world.forEach<Transform, MeshRenderer>([this, &world](Entity entity, Transform&, MeshRenderer& meshRenderer) {
        if (!meshRenderer.cachedMesh || !meshRenderer.cachedShader) {
            return;
        }

        if (!meshRenderer.cachedMesh->isLoaded() || !meshRenderer.cachedShader->isLoaded()) {
            return;
        }

        const MeshData* meshData = meshRenderer.cachedMesh->getData();
        const ShaderData* shaderData = meshRenderer.cachedShader->getData();
        if (meshData == nullptr || shaderData == nullptr || shaderData->programId == 0) {
            return;
        }

        std::unordered_set<Entity> visited;
        const Mat4 modelMatrix = buildWorldMatrix(world, entity, visited);

        renderer_.useShaderProgram(shaderData->programId);
        setupMatrices(world, shaderData->programId, modelMatrix);
        setupLighting(shaderData->programId);

        if (!meshData->subMeshes.empty()) {
            for (const SubMesh& subMesh : meshData->subMeshes) {
                renderSubMesh(subMesh, *shaderData, meshRenderer);
            }
        } else if (meshData->vao != 0 && meshData->indexCount > 0) {
            if (meshRenderer.cachedBaseColorTexture && meshRenderer.cachedBaseColorTexture->isLoaded()) {
                renderer_.bindTexture2D(meshRenderer.cachedBaseColorTexture->getData()->textureId, 0);
                renderer_.setInt(shaderData->programId, "baseColorTexture", 0);
            }

            renderer_.drawIndexed(meshData->vao, meshData->indexCount);
        }

        renderer_.useShaderProgram(0);
        renderer_.bindTexture2D(0, 0);
    });
}

void RenderSystem::setupLighting(unsigned int shaderProgram) {
    renderer_.setVec3(shaderProgram, "light.direction", Vec3{-0.5f, -1.0f, -0.3f});
    renderer_.setVec3(shaderProgram, "light.color", Vec3{1.0f, 0.96f, 0.9f});
    renderer_.setFloat(shaderProgram, "light.ambientStrength", 0.35f);
    renderer_.setFloat(shaderProgram, "light.diffuseStrength", 0.95f);
}

void RenderSystem::renderSubMesh(const SubMesh& subMesh, const ShaderData& shaderData, const MeshRenderer& meshRenderer) {
    std::shared_ptr<Resource<TextureData>> texture = meshRenderer.cachedBaseColorTexture;
    if ((!texture || !texture->isLoaded()) && !subMesh.material.diffuseTexturePath.empty()) {
        texture = ResourceManager::getInstance().load<TextureData>(subMesh.material.diffuseTexturePath);
    }

    if (texture && texture->isLoaded()) {
        renderer_.bindTexture2D(texture->getData()->textureId, 0);
        renderer_.setInt(shaderData.programId, "baseColorTexture", 0);
    } else {
        renderer_.bindTexture2D(0, 0);
    }

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
        viewMatrix.data()[14] = -5.0f;
        int width = 0;
        int height = 0;
        renderer_.getFramebufferSize(width, height);
        const float aspect = (width > 0 && height > 0) ? static_cast<float>(width) / static_cast<float>(height) : (800.0f / 600.0f);
        projectionMatrix = Math::perspective(45.0f * 3.1415926f / 180.0f, aspect, 0.1f, 100.0f);
    }

    renderer_.setMatrix4(shaderProgram, "view", viewMatrix);
    renderer_.setMatrix4(shaderProgram, "projection", projectionMatrix);
}
