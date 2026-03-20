#include "ecs/RenderSystem.h"
#include "ecs/Components.h"
#include "ecs/World.h"
#include "math/MathTypes.h"
#include "render/IRenderAdapter.h"
#include "resources/ResourceManager.h"
#include "core/Logger.h"

#include <unordered_set>
#include <glad/glad.h>

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
            const Mat4 parentMatrix = buildWorldMatrix(world, hierarchy.parent, visited);
            worldMatrix = Math::multiply(parentMatrix, worldMatrix);
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
    world.forEach<Transform, MeshRenderer>([this, &world](Entity entity, Transform& transform, MeshRenderer& meshRenderer) {
        (void)transform;
        std::unordered_set<Entity> visited;
        const Mat4 modelMatrix = buildWorldMatrix(world, entity, visited);
        
        // Если указан путь к мешу, используем загруженную модель
        if (!meshRenderer.meshPath.empty()) {
            // Загружаем ресурсы только один раз
            if (!meshRenderer.resourcesLoaded) {
                auto& resMgr = ResourceManager::getInstance();
                meshRenderer.cachedMesh = resMgr.loadMesh(meshRenderer.meshPath);
                meshRenderer.cachedShader = resMgr.loadShader(meshRenderer.vertexShaderPath, meshRenderer.fragmentShaderPath);
                meshRenderer.resourcesLoaded = true;
                
                if (!meshRenderer.cachedMesh || !meshRenderer.cachedShader) {
                    LOG_ERROR("Failed to load mesh or shader resources");
                    return;
                }
            }
            
            // Используем кэшированные ресурсы
            if (meshRenderer.cachedMesh && meshRenderer.cachedShader && 
                meshRenderer.cachedMesh->isLoaded() && meshRenderer.cachedShader->isLoaded()) {
                
                const auto* meshData = meshRenderer.cachedMesh->getData();
                const auto* shaderData = meshRenderer.cachedShader->getData();
                
                if (shaderData->programId == 0) {
                    LOG_ERROR("Invalid shader program");
                    return;
                }
                
                // Используем шейдер
                glUseProgram(shaderData->programId);
                
                // Устанавливаем матрицы
                int modelLoc = glGetUniformLocation(shaderData->programId, "model");
                if (modelLoc != -1) {
                    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, modelMatrix.data());
                }
                
                // View матрица
                Mat4 viewMatrix = Mat4::identity();
                viewMatrix.data()[14] = -3.0f;
                int viewLoc = glGetUniformLocation(shaderData->programId, "view");
                if (viewLoc != -1) {
                    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, viewMatrix.data());
                }
                
                // Projection матрица
                float fov = 45.0f * 3.14159f / 180.0f;
                float aspect = 800.0f / 600.0f;
                float near = 0.1f;
                float far = 100.0f;
                Mat4 projMatrix = Math::perspective(fov, aspect, near, far);
                int projLoc = glGetUniformLocation(shaderData->programId, "projection");
                if (projLoc != -1) {
                    glUniformMatrix4fv(projLoc, 1, GL_FALSE, projMatrix.data());
                }
                
                // Рендерим модель с submeshes (если есть)
                if (meshData->hasMultipleMaterials()) {
                    renderSubMeshesWithOverride(meshData, shaderData, meshRenderer);
                } else {
                    // Старый путь для простых моделей
                    renderSimpleMesh(meshData, shaderData, meshRenderer);
                }
                
                // Сбрасываем состояние
                glUseProgram(0);
            }
        } else {
            // Обратная совместимость - рисуем примитив
            this->renderer_.drawPrimitive(meshRenderer.primitive, modelMatrix, meshRenderer.color);
        }
    });
}

void RenderSystem::renderSubMeshes(const MeshData* meshData, const ShaderData* shaderData) {
    auto& resMgr = ResourceManager::getInstance();
    
    LOG_INFO("Rendering " + std::to_string(meshData->subMeshes.size()) + " submeshes");
    
    for (size_t i = 0; i < meshData->subMeshes.size(); ++i) {
        const auto& subMesh = meshData->subMeshes[i];
        if (subMesh.vao == 0) {
            LOG_ERROR("SubMesh " + std::to_string(i) + " has invalid VAO");
            continue;
        }
        
        LOG_INFO("SubMesh " + std::to_string(i) + ": material=" + subMesh.material.name + 
                 ", texture=" + subMesh.material.diffuseTexturePath);
        
        // Загружаем текстуру для этого submesh
        if (!subMesh.material.diffuseTexturePath.empty()) {
            auto texture = resMgr.loadTexture(subMesh.material.diffuseTexturePath);
            if (texture && texture->isLoaded()) {
                const auto* texData = texture->getData();
                LOG_INFO("  Binding texture ID=" + std::to_string(texData->textureId));
                
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, texData->textureId);
                
                int texLoc = glGetUniformLocation(shaderData->programId, "baseColorTexture");
                if (texLoc != -1) {
                    glUniform1i(texLoc, 0);
                } else {
                    LOG_ERROR("  Uniform 'baseColorTexture' not found in shader");
                }
            } else {
                LOG_ERROR("  Failed to load texture: " + subMesh.material.diffuseTexturePath);
            }
        } else {
            LOG_ERROR("  No diffuse texture path for material: " + subMesh.material.name);
        }
        
        // Рисуем submesh
        glBindVertexArray(subMesh.vao);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(subMesh.indices.size()), 
                      GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
    
    // Сбрасываем текстуры
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void RenderSystem::renderSubMeshesWithOverride(const MeshData* meshData, const ShaderData* shaderData,
                                                const MeshRenderer& meshRenderer) {
    auto& resMgr = ResourceManager::getInstance();
    
    // Если в MeshRenderer указана текстура, используем её для всех submeshes
    std::shared_ptr<Resource<TextureData>> overrideTexture;
    if (!meshRenderer.baseColorTexturePath.empty()) {
        overrideTexture = resMgr.loadTexture(meshRenderer.baseColorTexturePath);
    }
    
    for (size_t i = 0; i < meshData->subMeshes.size(); ++i) {
        const auto& subMesh = meshData->subMeshes[i];
        if (subMesh.vao == 0) continue;
        
        // Приоритет: override текстура > текстура из материала
        std::shared_ptr<Resource<TextureData>> textureToUse;
        if (overrideTexture && overrideTexture->isLoaded()) {
            textureToUse = overrideTexture;
        } else if (!subMesh.material.diffuseTexturePath.empty()) {
            textureToUse = resMgr.loadTexture(subMesh.material.diffuseTexturePath);
        }
        
        // Применяем текстуру
        if (textureToUse && textureToUse->isLoaded()) {
            const auto* texData = textureToUse->getData();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texData->textureId);
            
            int texLoc = glGetUniformLocation(shaderData->programId, "baseColorTexture");
            if (texLoc != -1) {
                glUniform1i(texLoc, 0);
            }
        }
        
        // Рисуем submesh
        glBindVertexArray(subMesh.vao);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(subMesh.indices.size()), 
                      GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
    
    // Сбрасываем текстуры
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void RenderSystem::renderSimpleMesh(const MeshData* meshData, const ShaderData* shaderData, 
                                    const MeshRenderer& meshRenderer) {
    // Применяем текстуры из MeshRenderer (старый способ)
    auto& resMgr = ResourceManager::getInstance();
    
    auto bindTexture = [&](const std::string& path, const char* uniformName, int unit) {
        if (!path.empty()) {
            auto tex = resMgr.loadTexture(path);
            if (tex && tex->isLoaded()) {
                const auto* texData = tex->getData();
                glActiveTexture(GL_TEXTURE0 + unit);
                glBindTexture(GL_TEXTURE_2D, texData->textureId);
                
                int texLoc = glGetUniformLocation(shaderData->programId, uniformName);
                if (texLoc != -1) {
                    glUniform1i(texLoc, unit);
                }
            }
        }
    };
    
    bindTexture(meshRenderer.baseColorTexturePath, "baseColorTexture", 0);
    
    // Рисуем меш
    if (meshData->vao != 0) {
        glBindVertexArray(meshData->vao);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(meshData->indices.size()), 
                      GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
    
    // Сбрасываем текстуры
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}
