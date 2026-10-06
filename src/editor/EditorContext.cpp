#include "editor/EditorContext.h"

#include "animation/Animation.h"
#include "core/Logger.h"
#include "core/ServiceLocator.h"
#include "editor/EditorMath.h"
#include "editor/MeshBounds.h"
#include "events/CollisionEvent.h"
#include "input/InputManager.h"
#include "input/KeyCode.h"
#include "math/CameraMath.h"
#include "render/IRenderAdapter.h"
#include "resources/HotReload.h"
#include "resources/ResourceManager.h"
#include "resources/SceneManifest.h"

#include <imgui.h>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <limits>

using namespace EditorMath;

namespace {
constexpr float kMoveSpeed = 1.0f;
constexpr float kJumpSpeed = 3.5f;
constexpr float kScaleStep = 0.1f;
constexpr float kRotationStep = kPi / 4.0f;
constexpr float kMinScale = 0.1f;
constexpr const char* kDefaultScenePath = "assets/scenes/demo_scene.json";
constexpr const char* kCubeMeshPath = "primitive:cube";
constexpr const char* kCubeTexturePath = "assets/textures/WoodCrate02.dds";
constexpr const char* kVertexShaderPath = "assets/shaders/mesh_vertex.glsl";
constexpr const char* kFragmentShaderPath = "assets/shaders/mesh_fragment_simple_texture.glsl";
// Против направления света из RenderSystem::setupLighting — туда, где висит солнце.
constexpr Vec3 kSunDirection{0.5f, -0.3f, 1.0f};

bool isApproximately(float left, float right) {
    return std::abs(left - right) < 0.0001f;
}

bool isDefaultBoxCollider(const Collider& collider) {
    return collider.type == ColliderType::Box &&
        isApproximately(collider.halfExtents.x, 0.5f) &&
        isApproximately(collider.halfExtents.y, 0.5f) &&
        isApproximately(collider.halfExtents.z, 0.5f) &&
        isApproximately(collider.offset.x, 0.0f) &&
        isApproximately(collider.offset.y, 0.0f) &&
        isApproximately(collider.offset.z, 0.0f);
}

bool extractMeshBounds(const MeshData& meshData, Vec3& outCenter, Vec3& outHalfExtents) {
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();
    float maxZ = -std::numeric_limits<float>::max();
    bool hasVertices = false;

    auto consumeVertices = [&](const std::vector<Vertex>& vertices) {
        for (const Vertex& vertex : vertices) {
            hasVertices = true;
            minX = std::min(minX, vertex.position.x);
            minY = std::min(minY, vertex.position.y);
            minZ = std::min(minZ, vertex.position.z);
            maxX = std::max(maxX, vertex.position.x);
            maxY = std::max(maxY, vertex.position.y);
            maxZ = std::max(maxZ, vertex.position.z);
        }
    };

    if (!meshData.subMeshes.empty()) {
        for (const SubMesh& subMesh : meshData.subMeshes) {
            consumeVertices(subMesh.vertices);
        }
    } else {
        consumeVertices(meshData.vertices);
    }

    if (!hasVertices) {
        return false;
    }

    outCenter = Vec3{(minX + maxX) * 0.5f, (minY + maxY) * 0.5f, (minZ + maxZ) * 0.5f};
    outHalfExtents = Vec3{(maxX - minX) * 0.5f, (maxY - minY) * 0.5f, (maxZ - minZ) * 0.5f};
    return true;
}

void fitColliderToMeshBounds(const MeshRenderer& renderer, Collider& collider) {
    if (!isDefaultBoxCollider(collider) || !renderer.cachedMesh || !renderer.cachedMesh->isLoaded()) {
        return;
    }

    const MeshData* meshData = renderer.cachedMesh->getData();
    if (meshData == nullptr) {
        return;
    }

    Vec3 boundsCenter{};
    Vec3 boundsHalfExtents{};
    if (!extractMeshBounds(*meshData, boundsCenter, boundsHalfExtents)) {
        return;
    }

    collider.offset = boundsCenter;
    collider.halfExtents = boundsHalfExtents;
}

}

EditorContext::EditorContext(IRenderAdapter& renderer)
    : renderer(renderer),
      previewer(renderer),
      renderSystem(renderer),
      debugRenderSystem(renderer),
      stress(renderer) {
}

void EditorContext::enter() {
    bindActions();
    loadScene(kDefaultScenePath);
    debugRenderSystem.setEnabled(false);

    ServiceLocator::getEventDispatcher().clear();
    ServiceLocator::getEventDispatcher().addListener<CollisionEvent>([this](const CollisionEvent& event) {
        LOG_INFO(
            "CollisionEvent: " +
            displayName(event.first) + " #" + std::to_string(event.first) +
            " <-> " +
            displayName(event.second) + " #" + std::to_string(event.second) +
            ", penetration=" +
            std::to_string(event.penetration));
    });
}

void EditorContext::exit() {
    previewer.release();
    ServiceLocator::getEventDispatcher().clear();
    world.clear();
    selected = kInvalidEntity;
    controllableEntity = kInvalidEntity;
    gameCameraEntity = kInvalidEntity;
    editorCameraEntity = kInvalidEntity;
}

void EditorContext::update(float dt) {
    ZoneScoped;
    world.forEach<MeshRenderer, Collider>([](Entity, MeshRenderer& mesh, Collider& collider) {
        if (!mesh.colliderBoundsInitialized && mesh.cachedMesh && mesh.cachedMesh->isLoaded()) {
            fitColliderToMeshBounds(mesh, collider);
            mesh.colliderBoundsInitialized = true;
        }
    });

    lastDt = dt;
    previewer.beginFrame();
    frameTimesMs[frameHistoryOffset] = dt * 1000.0f;
    frameHistoryOffset = (frameHistoryOffset + 1) % kFrameHistory;
    if (fpsAverage <= 0.0f && dt > 0.0f) {
        fpsAverage = 1.0f / dt;
        frameTimeAverageMs = dt * 1000.0f;
    }
    fpsAccumulator_ += dt;
    ++fpsFrames_;
    if (fpsAccumulator_ >= 0.5f) {
        fpsAverage = static_cast<float>(fpsFrames_) / fpsAccumulator_;
        frameTimeAverageMs = fpsAccumulator_ * 1000.0f / static_cast<float>(fpsFrames_);
        fpsAccumulator_ = 0.0f;
        fpsFrames_ = 0;
    }

    if (HotReload::getInstance().update()) {
        for (const auto& path : HotReload::getInstance().getChangedFiles()) {
            ResourceManager::getInstance().reloadShadersForFile(path);
        }
    }

    if (animationLoad && !animationLoad->isPending() && mode == EditorMode::Edit) {
        rebuildAnimationDemo();
    }
    heavyLoad.update();
    stress.onFrame(dt * 1000.0);
    fitPendingModels();

    const bool allowGameInput = gameViewInputActive && !ImGui::GetIO().WantTextInput;
    const bool simulate = mode == EditorMode::Play && (!paused || stepRequested_);
    // Шаг из паузы — ровно один кадр с фиксированным временем, как Step в Unity.
    const float simulationDt = paused ? 1.0f / 60.0f : dt;
    if (simulate) {
        updateGameplay(simulationDt, allowGameInput);
    }
    stepRequested_ = false;
    // Редактор показывает анимацию и вне Play; на паузе поза замирает.
    if (mode == EditorMode::Edit || simulate) {
        animationSystem.update(world, mode == EditorMode::Play ? simulationDt : dt);
    }
}

bool EditorContext::loadScene(const std::string& manifestPath) {
    cancelDragPreview();
    dropHighlight = kInvalidEntity;
    if (mode == EditorMode::Play) {
        stop();
    }
    scenePath_ = manifestPath;
    pendingFits_.clear();
    boundsCache_.clear();
    renamingEntity = kInvalidEntity;
    selectedAsset.clear();
    createScene();
    createGameCamera();
    createEditorCameraEntity();
    setCameraMode();
    placeGridOnGround();
    return true;
}

void EditorContext::placeGridOnGround() {
    // Сетку кладём на верх самого широкого неподвижного объекта под началом координат — «пол» сцены.
    float bestArea = 4.0f;
    float height = 0.0f;
    for (Entity entity : world.getEntities()) {
        if (isEditorEntity(entity) || world.hasComponent<Rigidbody>(entity) || !world.hasComponent<Transform>(entity) ||
            !world.hasComponent<MeshRenderer>(entity)) {
            continue;
        }
        const Transform& transform = world.getComponent<Transform>(entity);
        Vec3 bounds{std::abs(transform.scale.x) * 0.5f, std::abs(transform.scale.y) * 0.5f, std::abs(transform.scale.z) * 0.5f};
        AABB box{transform.position, bounds};
        worldBounds(entity, box);
        const float area = box.halfSize.x * box.halfSize.y * 4.0f;
        const bool underOrigin = std::abs(box.center.x) <= box.halfSize.x && std::abs(box.center.y) <= box.halfSize.y;
        if (area > bestArea && underOrigin && box.halfSize.z < box.halfSize.x && box.halfSize.z < box.halfSize.y) {
            bestArea = area;
            height = box.center.z + box.halfSize.z;
        }
    }
    sceneView.gridHeight = std::abs(height) < 50.0f ? height : 0.0f;
}

void EditorContext::reloadScene() {
    loadScene(scenePath_.empty() ? std::string(kDefaultScenePath) : scenePath_);
}

std::string EditorContext::sceneName() const {
    return scenePath_.empty() ? std::string("Untitled") : std::filesystem::path(scenePath_).stem().string();
}

void EditorContext::bindActions() {
    InputManager& inputManager = InputManager::getInstance();
    inputManager.bindAction("MoveLeft", KeyCode::Left);
    inputManager.bindAction("MoveLeft", KeyCode::A);
    inputManager.bindAction("MoveRight", KeyCode::Right);
    inputManager.bindAction("MoveRight", KeyCode::D);
    inputManager.bindAction("MoveForward", KeyCode::Up);
    inputManager.bindAction("MoveForward", KeyCode::W);
    inputManager.bindAction("MoveBackward", KeyCode::Down);
    inputManager.bindAction("MoveBackward", KeyCode::S);
    inputManager.bindAction("Jump", KeyCode::Space);
    inputManager.bindAction("CameraForward", KeyCode::W);
    inputManager.bindAction("CameraBackward", KeyCode::S);
    inputManager.bindAction("CameraLeft", KeyCode::A);
    inputManager.bindAction("CameraRight", KeyCode::D);
    inputManager.bindAction("CameraUp", KeyCode::E);
    inputManager.bindAction("CameraDown", KeyCode::Q);
}

void EditorContext::createScene() {
    animationDemoEntities.clear();
    world.clear();
    selected = kInvalidEntity;
    controllableEntity = kInvalidEntity;
    gameCameraEntity = kInvalidEntity;
    editorCameraEntity = kInvalidEntity;

    if (!createSceneFromManifest()) {
        createFallbackScene();
    }

    for (Entity entity : world.getEntities()) {
        if (!isEditorEntity(entity)) {
            selected = entity;
            break;
        }
    }
}

bool EditorContext::createSceneFromManifest() {
    auto& resourceManager = ResourceManager::getInstance();
    // Стартовую сцену заранее прочитал LoadingState; другие сцены маленькие — читаем сразу.
    auto scene = resourceManager.loadSceneAsync(scenePath_);
    SceneManifest parsed;
    const SceneManifest* manifestPtr = nullptr;
    if (scene && scene->isLoaded()) {
        manifestPtr = scene->getData();
    } else if (parsed.loadFromFile(scenePath_)) {
        manifestPtr = &parsed;
    } else {
        LOG_ERROR("EditorContext: failed to read scene " + scenePath_);
        return false;
    }
    const SceneManifest& manifest = *manifestPtr;

    for (const SceneEntityDescription& description : manifest.getEntities()) {
        const std::string* meshPath = manifest.findMeshPath(description.meshId);
        const ShaderAssetPaths* shaderPaths = manifest.findShader(description.shaderId);
        if (meshPath == nullptr || shaderPaths == nullptr) {
            LOG_ERROR("Scene entity has unresolved resources: " + description.tag);
            continue;
        }

        MeshRenderer meshRenderer;
        meshRenderer.meshId = description.meshId;
        meshRenderer.baseColorTextureId = description.baseColorTextureId;
        meshRenderer.shaderId = description.shaderId;
        meshRenderer.cachedMesh = resourceManager.loadMeshAsync(*meshPath);
        meshRenderer.cachedShader = resourceManager.loadShader(shaderPaths->vertexPath, shaderPaths->fragmentPath);

        if (!description.baseColorTextureId.empty()) {
            if (const std::string* texturePath = manifest.findTexturePath(description.baseColorTextureId)) {
                meshRenderer.cachedBaseColorTexture = resourceManager.loadTextureAsync(*texturePath, JobPriority::High);
            }
        }

        if (!meshRenderer.cachedMesh || !meshRenderer.cachedShader) {
            LOG_ERROR("Failed to load scene resources for entity: " + description.tag);
            continue;
        }

        Entity entity = world.createEntity();
        world.addComponent<Tag>(entity, Tag{description.tag});
        world.addComponent<Transform>(entity, Transform{description.position, description.rotation, description.scale});
        world.addComponent<MeshRenderer>(entity, meshRenderer);
        if (description.hasRigidbody) {
            world.addComponent<Rigidbody>(entity, description.rigidbody);
        }
        if (description.hasCollider) {
            Collider collider = description.collider;
            fitColliderToMeshBounds(meshRenderer, collider);
            world.addComponent<Collider>(entity, collider);
        }
        if (description.spinSpeed != 0.0f) {
            world.addComponent<Spin>(entity, Spin{description.spinSpeed});
        }

        if (description.controllable) {
            controllableEntity = entity;
            if (!world.hasComponent<Rigidbody>(entity)) {
                world.addComponent<Rigidbody>(entity, Rigidbody{Vec3{}, Vec3{}, 1.0f, true});
            }
            if (!world.hasComponent<Collider>(entity)) {
                Collider collider{ColliderType::Box, Vec3{0.5f, 0.5f, 0.5f}, Vec3{}, 0.5f};
                fitColliderToMeshBounds(meshRenderer, collider);
                world.addComponent<Collider>(entity, collider);
            }
        }

        HotReload::getInstance().watchFile(shaderPaths->vertexPath);
        HotReload::getInstance().watchFile(shaderPaths->fragmentPath);
    }

    return !world.getEntities().empty();
}

void EditorContext::createFallbackScene() {
    LOG_ERROR("EditorContext: falling back to resource-based test scene");

    auto& resourceManager = ResourceManager::getInstance();
    MeshRenderer meshRenderer;
    meshRenderer.meshId = "fallback_cube";
    meshRenderer.baseColorTextureId = "fallback_crate";
    meshRenderer.shaderId = "fallback_textured";
    meshRenderer.cachedMesh = resourceManager.loadMeshAsync(kCubeMeshPath);
    meshRenderer.cachedBaseColorTexture = resourceManager.loadTextureAsync(kCubeTexturePath, JobPriority::High);
    meshRenderer.cachedShader = resourceManager.loadShader(kVertexShaderPath, kFragmentShaderPath);

    if (!meshRenderer.cachedMesh || !meshRenderer.cachedShader || !meshRenderer.cachedBaseColorTexture) {
        LOG_ERROR("EditorContext: failed to build fallback scene resources");
        return;
    }

    HotReload::getInstance().watchFile(kVertexShaderPath);
    HotReload::getInstance().watchFile(kFragmentShaderPath);

    controllableEntity = world.createEntity();
    world.addComponent<Tag>(controllableEntity, Tag{"FallbackCube"});
    world.addComponent<Transform>(controllableEntity, Transform{{0.0f, 0.0f, 0.0f}, {}, {0.8f, 0.8f, 0.8f}});
    world.addComponent<MeshRenderer>(controllableEntity, meshRenderer);
    world.addComponent<Rigidbody>(controllableEntity, Rigidbody{Vec3{}, Vec3{}, 1.0f, true});
    Collider collider{ColliderType::Box, Vec3{0.5f, 0.5f, 0.5f}, Vec3{}, 0.5f};
    fitColliderToMeshBounds(meshRenderer, collider);
    world.addComponent<Collider>(controllableEntity, collider);
}

Entity EditorContext::createEmpty(const std::string& name, const Vec3& position) {
    const Entity entity = world.createEntity();
    world.addComponent<Tag>(entity, Tag{name});
    world.addComponent<Transform>(entity, Transform{position, {}, {1.0f, 1.0f, 1.0f}});
    select(entity);
    return entity;
}

Entity EditorContext::createCube(const std::string& name, const Vec3& position) {
    ResourceManager& resourceManager = ResourceManager::getInstance();

    MeshRenderer meshRenderer;
    meshRenderer.meshId = kCubeMeshPath;
    meshRenderer.baseColorTextureId = kCubeTexturePath;
    meshRenderer.shaderId = ResourceManager::makeShaderKey(kVertexShaderPath, kFragmentShaderPath);
    meshRenderer.cachedMesh = resourceManager.loadMeshAsync(kCubeMeshPath);
    meshRenderer.cachedBaseColorTexture = resourceManager.loadTextureAsync(kCubeTexturePath, JobPriority::High);
    meshRenderer.cachedShader = resourceManager.loadShader(kVertexShaderPath, kFragmentShaderPath);

    if (!meshRenderer.cachedMesh || !meshRenderer.cachedBaseColorTexture || !meshRenderer.cachedShader) {
        LOG_ERROR("EditorContext: failed to create cube entity resources");
        return kInvalidEntity;
    }

    const Entity entity = world.createEntity();
    world.addComponent<Tag>(entity, Tag{name});
    world.addComponent<Transform>(entity, Transform{position, {}, {1.0f, 1.0f, 1.0f}});
    world.addComponent<MeshRenderer>(entity, meshRenderer);
    Collider collider{ColliderType::Box, Vec3{0.5f, 0.5f, 0.5f}, Vec3{}, 0.5f};
    fitColliderToMeshBounds(meshRenderer, collider);
    world.addComponent<Collider>(entity, collider);

    HotReload::getInstance().watchFile(kVertexShaderPath);
    HotReload::getInstance().watchFile(kFragmentShaderPath);

    select(entity);
    return entity;
}

Entity EditorContext::createSphere(const std::string& name, const Vec3& position) {
    ResourceManager& resourceManager = ResourceManager::getInstance();
    MeshRenderer meshRenderer;
    meshRenderer.meshId = "primitive:sphere";
    meshRenderer.shaderId = ResourceManager::makeShaderKey(kVertexShaderPath, kFragmentShaderPath);
    meshRenderer.cachedMesh = resourceManager.loadMeshAsync(meshRenderer.meshId);
    meshRenderer.cachedShader = resourceManager.loadShader(kVertexShaderPath, kFragmentShaderPath);
    if (!meshRenderer.cachedMesh || !meshRenderer.cachedShader) {
        LOG_ERROR("EditorContext: failed to create sphere entity resources");
        return kInvalidEntity;
    }
    const Entity entity = world.createEntity();
    world.addComponent<Tag>(entity, Tag{name});
    world.addComponent<Transform>(entity, Transform{position, {}, {1.0f, 1.0f, 1.0f}});
    world.addComponent<MeshRenderer>(entity, meshRenderer);
    world.addComponent<Collider>(entity, Collider{ColliderType::Sphere, Vec3{0.5f, 0.5f, 0.5f}, Vec3{}, 0.5f});
    // Сфера уже совпадает с коллайдером — подгонка под меш не нужна.
    world.getComponent<MeshRenderer>(entity).colliderBoundsInitialized = true;
    select(entity);
    return entity;
}

Entity EditorContext::createModel(const std::string& path, const Vec3& position) {
    ResourceManager& resourceManager = ResourceManager::getInstance();
    MeshRenderer meshRenderer;
    meshRenderer.meshId = path;
    meshRenderer.shaderId = ResourceManager::makeShaderKey(kVertexShaderPath, kFragmentShaderPath);
    meshRenderer.cachedMesh = resourceManager.loadMeshAsync(path);
    meshRenderer.cachedShader = resourceManager.loadShader(kVertexShaderPath, kFragmentShaderPath);
    if (!meshRenderer.cachedMesh || !meshRenderer.cachedShader) {
        LOG_ERROR("EditorContext: failed to load model " + path);
        return kInvalidEntity;
    }

    const Entity entity = world.createEntity();
    world.addComponent<Tag>(entity, Tag{std::filesystem::path(path).stem().string()});
    world.addComponent<Transform>(entity, Transform{position, {}, {1.0f, 1.0f, 1.0f}});
    world.addComponent<MeshRenderer>(entity, meshRenderer);
    pendingFits_.push_back(PendingModelFit{entity, position});
    select(entity);
    LOG_INFO("Editor: added model " + path);
    return entity;
}

bool EditorContext::computeModelFit(const MeshRenderer& meshRenderer, Vec3& outRotation, float& outScale, Vec3& outOffset) const {
    outRotation = {};
    // Assimp отдаёт модели в Y-up, мир движка — Z-up.
    if (MeshBounds::isImportedModel(meshRenderer.meshId)) {
        outRotation.x = kPi * 0.5f;
    }
    outScale = 1.0f;
    outOffset = {};
    Vec3 localMin{};
    Vec3 localMax{};
    if (!localMeshBounds(meshRenderer, localMin, localMax)) {
        return false;
    }
    const Mat4 rotationOnly = Math::composeTransform({}, outRotation, {1.0f, 1.0f, 1.0f});
    const AABB rotated = transformBounds(rotationOnly, localMin, localMax);
    const float size = std::max({rotated.halfSize.x, rotated.halfSize.y, rotated.halfSize.z}) * 2.0f;
    // Единицы исходников гуляют (сантиметры FBX и т.п.) — подгоняем только явно нелепые размеры.
    if (size > 25.0f || (size > 0.0f && size < 0.05f)) {
        outScale = 2.0f / std::max(0.001f, rotated.halfSize.z * 2.0f);
    }
    // Точка опоры — центр низа габаритов.
    outOffset = {rotated.center.x * outScale, rotated.center.y * outScale, (rotated.center.z - rotated.halfSize.z) * outScale};
    return true;
}

void EditorContext::fitPendingModels() {
    for (auto it = pendingFits_.begin(); it != pendingFits_.end();) {
        const Entity entity = it->entity;
        if (!world.isAlive(entity) || !world.hasComponent<MeshRenderer>(entity)) {
            it = pendingFits_.erase(it);
            continue;
        }
        const MeshRenderer& meshRenderer = world.getComponent<MeshRenderer>(entity);
        if (!meshRenderer.cachedMesh || meshRenderer.cachedMesh->isPending()) {
            ++it;
            continue;
        }
        if (!meshRenderer.cachedMesh->isLoaded()) {
            LOG_ERROR("Editor: model failed to load: " + meshRenderer.meshId);
            it = pendingFits_.erase(it);
            continue;
        }

        Transform& transform = world.getComponent<Transform>(entity);
        Vec3 rotation{};
        float scale = 1.0f;
        Vec3 offset{};
        if (computeModelFit(meshRenderer, rotation, scale, offset)) {
            transform.rotation = rotation;
            transform.scale = {scale, scale, scale};
            transform.position = sub(it->groundPoint, offset);
            if (scale != 1.0f) {
                LOG_INFO("Editor: scaled " + displayName(entity) + " by " + std::to_string(scale) + " to fit the scene");
            }
        }
        const MeshData* data = meshRenderer.cachedMesh->getData();
        if (data && !data->skeleton.clips.empty() && !world.hasComponent<Animator>(entity)) {
            world.addComponent<Animator>(entity);
        }
        it = pendingFits_.erase(it);
    }
}

void EditorContext::updateDragPreview(const std::string& path, const Vec3& origin, const Vec3& direction) {
    if (isPlaying()) {
        return;
    }
    if (world.isAlive(dragPreviewEntity) && dragPreviewPath_ != path) {
        cancelDragPreview();
    }
    const Vec3 point = dropPoint(origin, direction, dragPreviewEntity);
    if (!world.isAlive(dragPreviewEntity)) {
        ResourceManager& resourceManager = ResourceManager::getInstance();
        MeshRenderer meshRenderer;
        meshRenderer.meshId = path;
        meshRenderer.shaderId = ResourceManager::makeShaderKey(kVertexShaderPath, kFragmentShaderPath);
        meshRenderer.cachedMesh = resourceManager.loadMeshAsync(path);
        meshRenderer.cachedShader = resourceManager.loadShader(kVertexShaderPath, kFragmentShaderPath);
        if (!meshRenderer.cachedMesh || !meshRenderer.cachedShader) {
            return;
        }
        dragPreviewEntity = world.createEntity();
        world.addComponent<Tag>(dragPreviewEntity, Tag{std::filesystem::path(path).stem().string()});
        Transform transform;
        if (MeshBounds::isImportedModel(path)) {
            transform.rotation.x = kPi * 0.5f;
        }
        world.addComponent<Transform>(dragPreviewEntity, transform);
        world.addComponent<MeshRenderer>(dragPreviewEntity, meshRenderer);
        dragPreviewPath_ = path;
        dragPreviewFitted_ = false;
        dragPreviewOffset_ = {};
    }

    const MeshRenderer& meshRenderer = world.getComponent<MeshRenderer>(dragPreviewEntity);
    if (!dragPreviewFitted_ && meshRenderer.cachedMesh && meshRenderer.cachedMesh->isLoaded()) {
        Vec3 rotation{};
        float scale = 1.0f;
        if (computeModelFit(meshRenderer, rotation, scale, dragPreviewOffset_)) {
            Transform& transform = world.getComponent<Transform>(dragPreviewEntity);
            transform.rotation = rotation;
            transform.scale = {scale, scale, scale};
        }
        const MeshData* data = meshRenderer.cachedMesh->getData();
        if (data && !data->skeleton.clips.empty() && !world.hasComponent<Animator>(dragPreviewEntity)) {
            world.addComponent<Animator>(dragPreviewEntity);
        }
        dragPreviewFitted_ = true;
    }
    // Пока меш грузится, RenderSystem рисует на его месте куб-заглушку.
    world.getComponent<Transform>(dragPreviewEntity).position = sub(point, dragPreviewOffset_);
    dragPreviewPoint_ = point;
}

Entity EditorContext::commitDragPreview() {
    const Entity entity = dragPreviewEntity;
    dragPreviewEntity = kInvalidEntity;
    if (!world.isAlive(entity)) {
        return kInvalidEntity;
    }
    if (!dragPreviewFitted_) {
        // Модель ещё грузится — подгоним, когда загрузится, на ту же точку.
        pendingFits_.push_back(PendingModelFit{entity, dragPreviewPoint_});
    }
    select(entity);
    LOG_INFO("Editor: added model " + dragPreviewPath_);
    return entity;
}

void EditorContext::cancelDragPreview() {
    if (world.isAlive(dragPreviewEntity)) {
        world.destroyEntity(dragPreviewEntity);
    }
    dragPreviewEntity = kInvalidEntity;
}

bool EditorContext::applyAssetToEntity(const std::string& path, Entity entity) {
    if (!isEditable(entity) || !world.hasComponent<MeshRenderer>(entity)) {
        return false;
    }
    std::string texture;
    const AssetType type = AssetDatabase::classify(path);
    if (type == AssetType::Texture) {
        texture = path;
    } else if (type == AssetType::Material) {
        MtlInfo info;
        if (readMtl(path, info)) {
            for (const MtlInfo::Entry& material : info.materials) {
                if (!material.diffuseTexture.empty()) {
                    texture = material.diffuseTexture;
                    break;
                }
            }
        }
    }
    if (texture.empty()) {
        LOG_WARN("Editor: " + path + " has no diffuse texture to apply");
        return false;
    }
    MeshRenderer& meshRenderer = world.getComponent<MeshRenderer>(entity);
    meshRenderer.baseColorTextureId = texture;
    meshRenderer.cachedBaseColorTexture = ResourceManager::getInstance().loadTextureAsync(texture, JobPriority::High);
    return true;
}

Entity EditorContext::duplicate(Entity source) {
    if (!isEditable(source)) {
        return kInvalidEntity;
    }

    std::function<Entity(Entity, Entity, bool)> copy = [&](Entity from, Entity newParent, bool isRoot) -> Entity {
        const Entity entity = world.createEntity();
        if (world.hasComponent<Tag>(from)) {
            world.addComponent<Tag>(entity, Tag{world.getComponent<Tag>(from).name + (isRoot ? " Copy" : "")});
        } else {
            world.addComponent<Tag>(entity, Tag{"Entity Copy"});
        }
        if (world.hasComponent<Transform>(from)) {
            Transform transform = world.getComponent<Transform>(from);
            if (isRoot) {
                transform.position.x += 1.0f;
                transform.position.y -= 1.0f;
            }
            world.addComponent<Transform>(entity, transform);
        }
        if (world.hasComponent<MeshRenderer>(from)) {
            world.addComponent<MeshRenderer>(entity, world.getComponent<MeshRenderer>(from));
        }
        if (world.hasComponent<Animator>(from)) {
            world.addComponent<Animator>(entity, world.getComponent<Animator>(from));
        }
        if (world.hasComponent<Rigidbody>(from)) {
            Rigidbody rigidbody = world.getComponent<Rigidbody>(from);
            rigidbody.velocity = Vec3{};
            rigidbody.acceleration = Vec3{};
            world.addComponent<Rigidbody>(entity, rigidbody);
        }
        if (world.hasComponent<Collider>(from)) {
            world.addComponent<Collider>(entity, world.getComponent<Collider>(from));
        }
        if (world.hasComponent<Spin>(from)) {
            world.addComponent<Spin>(entity, world.getComponent<Spin>(from));
        }
        if (world.hasComponent<Camera>(from)) {
            Camera camera = world.getComponent<Camera>(from);
            camera.active = false;
            world.addComponent<Camera>(entity, camera);
        }
        if (newParent != kInvalidEntity) {
            if (!world.hasComponent<Hierarchy>(newParent)) {
                world.addComponent<Hierarchy>(newParent);
            }
            world.getComponent<Hierarchy>(newParent).children.push_back(entity);
            world.addComponent<Hierarchy>(entity).parent = newParent;
        }
        for (Entity child : childrenOf(from)) {
            copy(child, entity, false);
        }
        return entity;
    };

    const Entity entity = copy(source, parentOf(source), true);
    select(entity);
    return entity;
}

void EditorContext::destroy(Entity entity) {
    if (!isEditable(entity)) {
        return;
    }
    for (Entity child : childrenOf(entity)) {
        destroy(child);
    }
    if (selected == entity) {
        selected = kInvalidEntity;
    }
    if (renamingEntity == entity) {
        renamingEntity = kInvalidEntity;
    }
    if (controllableEntity == entity) {
        controllableEntity = kInvalidEntity;
    }
    if (gameCameraEntity == entity) {
        gameCameraEntity = kInvalidEntity;
    }
    animationDemoEntities.erase(
        std::remove(animationDemoEntities.begin(), animationDemoEntities.end(), entity), animationDemoEntities.end());
    world.destroyEntity(entity);
}

Entity EditorContext::parentOf(Entity entity) const {
    if (!world.isAlive(entity) || !world.hasComponent<Hierarchy>(entity)) {
        return kInvalidEntity;
    }
    const Entity parent = world.getComponent<Hierarchy>(entity).parent;
    return world.isAlive(parent) ? parent : kInvalidEntity;
}

std::vector<Entity> EditorContext::childrenOf(Entity entity) const {
    std::vector<Entity> children;
    if (!world.isAlive(entity) || !world.hasComponent<Hierarchy>(entity)) {
        return children;
    }
    for (Entity child : world.getComponent<Hierarchy>(entity).children) {
        if (world.isAlive(child) && !isEditorEntity(child)) {
            children.push_back(child);
        }
    }
    return children;
}

std::vector<Entity> EditorContext::rootEntities() const {
    std::vector<Entity> roots;
    for (Entity entity : world.getEntities()) {
        if (isEditable(entity) && parentOf(entity) == kInvalidEntity) {
            roots.push_back(entity);
        }
    }
    return roots;
}

bool EditorContext::isAncestor(Entity ancestor, Entity entity) const {
    for (int depth = 0; depth < 256 && entity != kInvalidEntity; ++depth) {
        entity = parentOf(entity);
        if (entity == ancestor) {
            return true;
        }
    }
    return false;
}

bool EditorContext::setParent(Entity child, Entity parent) {
    if (!isEditable(child) || child == parent || (parent != kInvalidEntity && !isEditable(parent)) ||
        (parent != kInvalidEntity && isAncestor(child, parent)) || parentOf(child) == parent) {
        return false;
    }

    const Mat4 childWorld = worldMatrix(child);
    const Entity oldParent = parentOf(child);
    if (oldParent != kInvalidEntity) {
        auto& siblings = world.getComponent<Hierarchy>(oldParent).children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
    }
    if (!world.hasComponent<Hierarchy>(child)) {
        world.addComponent<Hierarchy>(child);
    }
    world.getComponent<Hierarchy>(child).parent = parent;
    if (parent != kInvalidEntity) {
        if (!world.hasComponent<Hierarchy>(parent)) {
            world.addComponent<Hierarchy>(parent);
        }
        world.getComponent<Hierarchy>(parent).children.push_back(child);
    }
    setWorldMatrix(child, childWorld);
    return true;
}

std::string EditorContext::displayName(Entity entity) const {
    if (world.isAlive(entity) && world.hasComponent<Tag>(entity)) {
        const Tag& tag = world.getComponent<Tag>(entity);
        if (!tag.name.empty()) {
            return tag.name;
        }
    }
    return "Entity " + std::to_string(entity);
}

bool EditorContext::isEditorEntity(Entity entity) const {
    return entity == editorCameraEntity;
}

bool EditorContext::isEditable(Entity entity) const {
    // Модель, которую ещё тащат из Content Browser, — не часть сцены, пока её не отпустили.
    return world.isAlive(entity) && !isEditorEntity(entity) && entity != dragPreviewEntity;
}

Vec3 EditorContext::defaultSpawnPosition() const {
    return add(camera.getPosition(), mul(camera.getForward(), 4.0f));
}

Mat4 EditorContext::worldMatrix(Entity entity) const {
    if (!world.isAlive(entity) || !world.hasComponent<Transform>(entity)) {
        return Mat4::identity();
    }
    const Transform& transform = world.getComponent<Transform>(entity);
    Mat4 result = Math::composeTransform(transform.position, transform.rotation, transform.scale);
    return Math::multiply(parentWorldMatrix(entity), result);
}

Mat4 EditorContext::parentWorldMatrix(Entity entity) const {
    Mat4 result = Mat4::identity();
    Entity current = parentOf(entity);
    for (int depth = 0; depth < 256 && current != kInvalidEntity && current != entity; ++depth) {
        if (world.hasComponent<Transform>(current)) {
            const Transform& transform = world.getComponent<Transform>(current);
            result = Math::multiply(Math::composeTransform(transform.position, transform.rotation, transform.scale), result);
        }
        current = parentOf(current);
    }
    return result;
}

void EditorContext::setWorldMatrix(Entity entity, const Mat4& worldValue) {
    if (!world.isAlive(entity)) {
        return;
    }
    if (!world.hasComponent<Transform>(entity)) {
        world.addComponent<Transform>(entity);
    }
    Transform& transform = world.getComponent<Transform>(entity);
    const Mat4 local = Math::multiply(inverse(parentWorldMatrix(entity)), worldValue);
    Vec3 rotation{};
    decompose(local, transform.position, rotation, transform.scale);
    transform.rotation = nearestEquivalentEuler(rotation, transform.rotation);
}

bool EditorContext::localMeshBounds(const MeshRenderer& meshRenderer, Vec3& outMin, Vec3& outMax) const {
    if (!meshRenderer.cachedMesh || !meshRenderer.cachedMesh->isLoaded()) {
        return false;
    }
    const MeshData* data = meshRenderer.cachedMesh->getData();
    if (data == nullptr) {
        return false;
    }
    // Ключ — адрес данных; число вершин отсекает чужой меш, занявший освобождённый адрес.
    const std::size_t vertices = MeshBounds::vertexCount(*data);
    auto it = boundsCache_.find(data);
    if (it == boundsCache_.end() || it->second.vertices != vertices) {
        CachedBounds bounds;
        bounds.vertices = vertices;
        if (!MeshBounds::computeBindPose(*data, bounds.min, bounds.max)) {
            return false;
        }
        it = boundsCache_.insert_or_assign(data, bounds).first;
    }
    outMin = it->second.min;
    outMax = it->second.max;
    return true;
}

bool EditorContext::worldBounds(Entity entity, AABB& outBounds) const {
    if (!world.isAlive(entity) || !world.hasComponent<Transform>(entity)) {
        return false;
    }
    const Mat4 matrix = worldMatrix(entity);
    if (world.hasComponent<MeshRenderer>(entity)) {
        Vec3 localMin{};
        Vec3 localMax{};
        if (localMeshBounds(world.getComponent<MeshRenderer>(entity), localMin, localMax)) {
            outBounds = transformBounds(matrix, localMin, localMax);
            return true;
        }
    }
    if (world.hasComponent<Collider>(entity)) {
        Transform worldTransform;
        decompose(matrix, worldTransform.position, worldTransform.rotation, worldTransform.scale);
        const Collider& collider = world.getComponent<Collider>(entity);
        if (collider.type == ColliderType::Box) {
            outBounds = CollisionUtils::buildAABB(worldTransform, collider);
        } else {
            const Sphere sphere = CollisionUtils::buildSphere(worldTransform, collider);
            outBounds = AABB{sphere.center, Vec3{sphere.radius, sphere.radius, sphere.radius}};
        }
        return true;
    }
    // Пустышки и камеры — маленький кубик вокруг точки, чтобы их можно было кликнуть.
    outBounds = AABB{transformPoint(matrix, {}), Vec3{0.25f, 0.25f, 0.25f}};
    return true;
}

Entity EditorContext::pick(const Vec3& origin, const Vec3& direction, float* outDistance, Entity ignore) const {
    Entity best = kInvalidEntity;
    float bestDistance = std::numeric_limits<float>::max();
    for (Entity entity : world.getEntities()) {
        // Скрытые глазиком объекты не ловят клики, как в Unity.
        if (isEditorEntity(entity) || entity == ignore ||
            (world.hasComponent<MeshRenderer>(entity) && !world.getComponent<MeshRenderer>(entity).visible)) {
            continue;
        }
        AABB bounds;
        if (!worldBounds(entity, bounds)) {
            continue;
        }
        float distance = 0.0f;
        if (rayIntersectsAABB(origin, direction, bounds, distance) && distance < bestDistance) {
            bestDistance = distance;
            best = entity;
        }
    }
    if (outDistance) {
        *outDistance = bestDistance;
    }
    return best;
}

Vec3 EditorContext::dropPoint(const Vec3& origin, const Vec3& direction, Entity ignore) const {
    float distance = 0.0f;
    if (pick(origin, direction, &distance, ignore) != kInvalidEntity && distance < 1000.0f) {
        return add(origin, mul(direction, distance));
    }
    if (std::abs(direction.z) > 0.0001f) {
        const float t = (sceneView.gridHeight - origin.z) / direction.z;
        if (t > 0.0f && t < 500.0f) {
            return add(origin, mul(direction, t));
        }
    }
    return add(origin, mul(direction, 6.0f));
}

void EditorContext::focusSelection() {
    AABB bounds;
    if (!worldBounds(selected, bounds)) {
        return;
    }
    // Радиус описанной сферы с запасом: объект целиком в кадре и с воздухом вокруг.
    const float radius = std::max(length(bounds.halfSize) * 1.4f, 1.2f);
    camera.focus(bounds.center, radius);
}

void EditorContext::beginRename(Entity entity) {
    if (!isEditable(entity)) {
        return;
    }
    renamingEntity = entity;
    renameNeedsFocus = true;
    renameBuffer.fill('\0');
    const std::string name = world.hasComponent<Tag>(entity) ? world.getComponent<Tag>(entity).name : displayName(entity);
    std::snprintf(renameBuffer.data(), renameBuffer.size(), "%s", name.c_str());
}

void EditorContext::commitRename() {
    if (isEditable(renamingEntity)) {
        if (!world.hasComponent<Tag>(renamingEntity)) {
            world.addComponent<Tag>(renamingEntity, Tag{});
        }
        world.getComponent<Tag>(renamingEntity).name = renameBuffer.data();
    }
    renamingEntity = kInvalidEntity;
}

void EditorContext::cancelRename() {
    renamingEntity = kInvalidEntity;
}

void EditorContext::select(Entity entity) {
    selected = world.isAlive(entity) && !isEditorEntity(entity) ? entity : kInvalidEntity;
    selectedAsset.clear();
}

void EditorContext::selectAsset(const std::string& path) {
    selectedAsset = path;
    selected = kInvalidEntity;
}

void EditorContext::clearSelection() {
    selected = kInvalidEntity;
    selectedAsset.clear();
}

void EditorContext::play() {
    if (mode == EditorMode::Play) {
        return;
    }
    if (renamingEntity != kInvalidEntity) {
        commitRename();
    }
    cancelDragPreview();
    dropHighlight = kInvalidEntity;
    playSnapshot_ = captureSnapshot();
    mode = EditorMode::Play;
    setCameraMode();
    updateGameCamera(0.0f, false);
    lmbWasPressed_ = false;
    rmbWasPressed_ = false;
    mmbWasPressed_ = false;
    LOG_INFO("Editor: entered Play mode");
}

void EditorContext::stop() {
    if (mode != EditorMode::Play) {
        return;
    }
    restoreSnapshot(playSnapshot_);
    mode = EditorMode::Edit;
    paused = false;
    createEditorCameraEntity();
    setCameraMode();
    LOG_INFO("Editor: left Play mode, scene restored");
}

void EditorContext::setPaused(bool value) {
    paused = value;
}

void EditorContext::stepFrame() {
    if (mode == EditorMode::Play) {
        paused = true;
        stepRequested_ = true;
    }
}

void EditorContext::createGameCamera() {
    gameCameraEntity = world.createEntity();
    world.addComponent<Tag>(gameCameraEntity, Tag{"Main Camera"});
    world.addComponent<Transform>(gameCameraEntity, Transform{
        Vec3{0.0f, -10.0f, 4.0f},
        Vec3{-0.3f, 0.0f, 0.0f},
        Vec3{1.0f, 1.0f, 1.0f}
    });
    world.addComponent<Camera>(gameCameraEntity, Camera{45.0f, 0.1f, 100.0f, 800.0f / 600.0f, false, Mat4::identity(), Mat4::identity()});
}

void EditorContext::createEditorCameraEntity() {
    editorCameraEntity = world.createEntity();
    world.addComponent<Transform>(editorCameraEntity, Transform{});
    world.addComponent<Camera>(editorCameraEntity, Camera{45.0f, 0.1f, 200.0f, 800.0f / 600.0f, true, Mat4::identity(), Mat4::identity()});
    syncEditorCameraEntity(sceneViewWidth_, sceneViewHeight_);
}

void EditorContext::syncEditorCameraEntity(int width, int height) {
    if (!world.isAlive(editorCameraEntity) || !world.hasComponent<Camera>(editorCameraEntity)) {
        return;
    }
    Camera& editorCamera = world.getComponent<Camera>(editorCameraEntity);
    editorCamera.aspectRatio = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 800.0f / 600.0f;
    editorCamera.viewMatrix = camera.getViewMatrix();
    editorCamera.projectionMatrix = camera.getProjectionMatrix();
    if (world.hasComponent<Transform>(editorCameraEntity)) {
        world.getComponent<Transform>(editorCameraEntity).position = camera.getPosition();
    }
}

void EditorContext::setCameraMode() {
    activateCamera(mode == EditorMode::Edit ? editorCameraEntity : gameCameraEntity);
}

void EditorContext::activateCamera(Entity entity) {
    // RenderSystem рисует через первую активную камеру мира — оставляем активной ровно одну.
    world.forEach<Camera>([entity](Entity candidate, Camera& cameraComponent) {
        cameraComponent.active = candidate == entity;
    });
}

void EditorContext::updateEditorCamera(float dt, int width, int height, bool inputEnabled, float mouseWheel) {
    sceneViewWidth_ = std::max(1, width);
    sceneViewHeight_ = std::max(1, height);
    camera.update(dt, sceneViewWidth_, sceneViewHeight_, inputEnabled, mouseWheel);
    syncEditorCameraEntity(sceneViewWidth_, sceneViewHeight_);
}

void EditorContext::renderSceneView(int width, int height) {
    ZoneScopedN("Scene View");
    syncEditorCameraEntity(width, height);
    activateCamera(editorCameraEntity);
    renderer.beginViewportFrame(kSceneViewTarget, width, height, 0.155f, 0.16f, 0.175f);
    renderer.drawSky(camera.getViewMatrix(), camera.getProjectionMatrix(), camera.getPosition(), kSunDirection);
    renderSystem.render(world);
    sceneDrawnMeshes = renderSystem.getLastDrawnMeshCount();
    if (sceneView.showGrid) {
        renderer.drawGrid(camera.getViewMatrix(), camera.getProjectionMatrix(), camera.getPosition(), sceneView.gridHeight);
    }
    // Коллайдеры в Scene View рисует оверлей панели (сглаженные линии); DebugRenderSystem — в Game View.
    if (sceneView.showSelectionOutline && isEditable(selected) && renderer.selectionMaskProgram() != 0) {
        // Обводим выделенное вместе с потомками, как Unity.
        std::vector<Entity> outlined;
        std::vector<Entity> stack{selected};
        while (!stack.empty() && outlined.size() < 4096) {
            const Entity entity = stack.back();
            stack.pop_back();
            outlined.push_back(entity);
            for (Entity child : childrenOf(entity)) {
                stack.push_back(child);
            }
        }
        renderer.beginSelectionMask();
        renderSystem.renderMask(world, outlined, renderer.selectionMaskProgram());
        const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
        renderer.endSelectionMask(Vec4{1.0f, 0.6f, 0.18f, 1.0f}, 1.6f * std::max(1.0f, scale));
    }
    // Объект, на который сейчас бросят текстуру или материал, — синим контуром.
    if (isEditable(dropHighlight) && renderer.selectionMaskProgram() != 0) {
        renderer.beginSelectionMask();
        renderSystem.renderMask(world, {dropHighlight}, renderer.selectionMaskProgram());
        const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
        renderer.endSelectionMask(Vec4{0.36f, 0.62f, 1.0f, 1.0f}, 2.0f * std::max(1.0f, scale));
    }
    renderer.endViewportFrame();
    setCameraMode();
}

Mat4 EditorContext::gameViewMatrix() const {
    if (!world.isAlive(gameCameraEntity) || !world.hasComponent<Transform>(gameCameraEntity)) {
        return Mat4::identity();
    }
    const Mat4 matrix = worldMatrix(gameCameraEntity);
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{};
    decompose(matrix, position, rotation, scale);
    const Transform& transform = world.getComponent<Transform>(gameCameraEntity);
    // Камера смотрит по pitch = rotation.x и yaw = rotation.z своего трансформа.
    return CameraMath::view(position, transform.rotation.x, transform.rotation.z);
}

Mat4 EditorContext::gameProjectionMatrix(float aspect) const {
    if (!world.isAlive(gameCameraEntity) || !world.hasComponent<Camera>(gameCameraEntity)) {
        return Mat4::identity();
    }
    const Camera& gameCamera = world.getComponent<Camera>(gameCameraEntity);
    return Math::perspective(toRadians(gameCamera.fovDegrees), aspect, gameCamera.nearClip, gameCamera.farClip);
}

void EditorContext::renderGameView(int width, int height) {
    ZoneScopedN("Game View");
    const bool hasCamera = world.isAlive(gameCameraEntity) && world.hasComponent<Camera>(gameCameraEntity);
    if (hasCamera) {
        Camera& gameCamera = world.getComponent<Camera>(gameCameraEntity);
        gameCamera.aspectRatio = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
        gameCamera.viewMatrix = gameViewMatrix();
        gameCamera.projectionMatrix = gameProjectionMatrix(gameCamera.aspectRatio);
    }
    activateCamera(gameCameraEntity);
    if (hasCamera) {
        renderer.beginViewportFrame(kGameViewTarget, width, height, 0.32f, 0.44f, 0.6f);
        const Camera& gameCamera = world.getComponent<Camera>(gameCameraEntity);
        renderer.drawSky(gameCamera.viewMatrix, gameCamera.projectionMatrix, transformPoint(worldMatrix(gameCameraEntity), {}), kSunDirection);
        renderSystem.render(world);
        debugRenderSystem.render(world);
    } else {
        renderer.beginViewportFrame(kGameViewTarget, width, height, 0.0f, 0.0f, 0.0f);
    }
    renderer.endViewportFrame();
    setCameraMode();
}

void EditorContext::updateGameplay(float dt, bool allowInput) {
    ZoneScopedN("Simulation");
    if (allowInput) {
        processGameplayInput();
    }

    bool waitingForColliders = false;
    world.forEach<MeshRenderer, Collider>([&](Entity, MeshRenderer& mesh, Collider&) {
        waitingForColliders |= mesh.cachedMesh && mesh.cachedMesh->isPending();
    });
    if (!waitingForColliders) {
        physicsSystem.update(world, dt);
    }
    spinSystem.update(world, dt);
    updateGameCamera(dt, allowInput);
}

void EditorContext::updateGameCamera(float dt, bool allowInput) {
    if (!world.isAlive(gameCameraEntity) ||
        !world.hasComponent<Transform>(gameCameraEntity) ||
        !world.hasComponent<Camera>(gameCameraEntity)) {
        return;
    }

    Transform& transform = world.getComponent<Transform>(gameCameraEntity);
    if (!allowInput) {
        return;
    }

    InputManager& inputManager = InputManager::getInstance();
    float moveForward = 0.0f;
    float moveRight = 0.0f;
    float moveUp = 0.0f;
    if (inputManager.isActionDown("CameraForward")) moveForward += 1.0f;
    if (inputManager.isActionDown("CameraBackward")) moveForward -= 1.0f;
    if (inputManager.isActionDown("CameraRight")) moveRight += 1.0f;
    if (inputManager.isActionDown("CameraLeft")) moveRight -= 1.0f;
    if (inputManager.isActionDown("CameraUp")) moveUp += 1.0f;
    if (inputManager.isActionDown("CameraDown")) moveUp -= 1.0f;

    const float yaw = transform.rotation.z;
    const float forwardX = -std::sin(yaw);
    const float forwardY = std::cos(yaw);
    const float rightX = std::cos(yaw);
    const float rightY = std::sin(yaw);
    constexpr float cameraMoveSpeed = 3.5f;
    transform.position.x += (forwardX * moveForward + rightX * moveRight) * cameraMoveSpeed * dt;
    transform.position.z += moveUp * cameraMoveSpeed * dt;
    transform.position.y += (forwardY * moveForward + rightY * moveRight) * cameraMoveSpeed * dt;

    if (inputManager.isMouseButtonDown(KeyCode::MouseRight)) {
        const Vec2 mouseDelta = inputManager.getMouseDelta();
        transform.rotation.z -= mouseDelta.x * 0.003f;
        transform.rotation.x -= mouseDelta.y * 0.003f;
        transform.rotation.x = std::clamp(transform.rotation.x, -1.4f, 1.4f);
    }
}

void EditorContext::processGameplayInput() {
    if (!world.isAlive(controllableEntity) || !world.hasComponent<Rigidbody>(controllableEntity)) {
        return;
    }

    Transform& transform = world.getComponent<Transform>(controllableEntity);
    Rigidbody& rigidbody = world.getComponent<Rigidbody>(controllableEntity);
    InputManager& inputManager = InputManager::getInstance();

    float horizontalVelocity = 0.0f;
    float depthVelocity = 0.0f;
    if (inputManager.isActionDown("MoveLeft")) horizontalVelocity -= kMoveSpeed;
    if (inputManager.isActionDown("MoveRight")) horizontalVelocity += kMoveSpeed;
    if (inputManager.isActionDown("MoveForward")) depthVelocity -= kMoveSpeed;
    if (inputManager.isActionDown("MoveBackward")) depthVelocity += kMoveSpeed;

    rigidbody.velocity.x = horizontalVelocity;
    rigidbody.velocity.y = depthVelocity;

    if (inputManager.isActionPressed("Jump") && std::abs(transform.position.z) < 0.051f) {
        rigidbody.velocity.z = kJumpSpeed;
    }

    const bool lmbNow = inputManager.isMouseButtonDown(KeyCode::MouseLeft);
    if (lmbNow && !lmbWasPressed_) {
        transform.scale.x += kScaleStep;
        transform.scale.y += kScaleStep;
        transform.scale.z += kScaleStep;
    }
    lmbWasPressed_ = lmbNow;

    const bool rmbNow = inputManager.isMouseButtonDown(KeyCode::MouseRight);
    if (rmbNow && !rmbWasPressed_) {
        transform.rotation.z += kRotationStep;
    }
    rmbWasPressed_ = rmbNow;

    const bool mmbNow = inputManager.isMouseButtonDown(KeyCode::MouseMiddle);
    if (mmbNow && !mmbWasPressed_) {
        transform.scale.x = std::max(kMinScale, transform.scale.x - kScaleStep);
        transform.scale.y = std::max(kMinScale, transform.scale.y - kScaleStep);
        transform.scale.z = std::max(kMinScale, transform.scale.z - kScaleStep);
    }
    mmbWasPressed_ = mmbNow;
}

EditorContext::SceneSnapshot EditorContext::captureSnapshot() const {
    SceneSnapshot snapshot;
    snapshot.selected = selected;
    snapshot.controllableEntity = controllableEntity;
    snapshot.gameCameraEntity = gameCameraEntity;

    for (Entity entity : world.getEntities()) {
        if (isEditorEntity(entity)) {
            continue;
        }

        EntitySnapshot entitySnapshot;
        entitySnapshot.entity = entity;
        if (world.hasComponent<Tag>(entity)) {
            entitySnapshot.hasTag = true;
            entitySnapshot.tag = world.getComponent<Tag>(entity);
        }
        if (world.hasComponent<Transform>(entity)) {
            entitySnapshot.hasTransform = true;
            entitySnapshot.transform = world.getComponent<Transform>(entity);
        }
        if (world.hasComponent<MeshRenderer>(entity)) {
            entitySnapshot.hasMeshRenderer = true;
            entitySnapshot.meshRenderer = world.getComponent<MeshRenderer>(entity);
        }
        if (world.hasComponent<Animator>(entity)) {
            entitySnapshot.hasAnimator = true;
            entitySnapshot.animator = world.getComponent<Animator>(entity);
        }
        if (world.hasComponent<Hierarchy>(entity)) {
            entitySnapshot.hasHierarchy = true;
            entitySnapshot.hierarchy = world.getComponent<Hierarchy>(entity);
        }
        if (world.hasComponent<Spin>(entity)) {
            entitySnapshot.hasSpin = true;
            entitySnapshot.spin = world.getComponent<Spin>(entity);
        }
        if (world.hasComponent<Camera>(entity)) {
            entitySnapshot.hasCamera = true;
            entitySnapshot.camera = world.getComponent<Camera>(entity);
        }
        if (world.hasComponent<Rigidbody>(entity)) {
            entitySnapshot.hasRigidbody = true;
            entitySnapshot.rigidbody = world.getComponent<Rigidbody>(entity);
        }
        if (world.hasComponent<Collider>(entity)) {
            entitySnapshot.hasCollider = true;
            entitySnapshot.collider = world.getComponent<Collider>(entity);
        }

        snapshot.entities.push_back(entitySnapshot);
    }

    return snapshot;
}

void EditorContext::restoreSnapshot(const SceneSnapshot& snapshot) {
    world.clear();
    editorCameraEntity = kInvalidEntity;

    for (const EntitySnapshot& entitySnapshot : snapshot.entities) {
        world.createEntityWithId(entitySnapshot.entity);
    }

    for (const EntitySnapshot& entitySnapshot : snapshot.entities) {
        const Entity entity = entitySnapshot.entity;
        if (entitySnapshot.hasTag) world.addComponent<Tag>(entity, entitySnapshot.tag);
        if (entitySnapshot.hasTransform) world.addComponent<Transform>(entity, entitySnapshot.transform);
        if (entitySnapshot.hasMeshRenderer) world.addComponent<MeshRenderer>(entity, entitySnapshot.meshRenderer);
        if (entitySnapshot.hasAnimator) world.addComponent<Animator>(entity, entitySnapshot.animator);
        if (entitySnapshot.hasHierarchy) world.addComponent<Hierarchy>(entity, entitySnapshot.hierarchy);
        if (entitySnapshot.hasSpin) world.addComponent<Spin>(entity, entitySnapshot.spin);
        if (entitySnapshot.hasCamera) world.addComponent<Camera>(entity, entitySnapshot.camera);
        if (entitySnapshot.hasRigidbody) world.addComponent<Rigidbody>(entity, entitySnapshot.rigidbody);
        if (entitySnapshot.hasCollider) world.addComponent<Collider>(entity, entitySnapshot.collider);
    }

    selected = world.isAlive(snapshot.selected) ? snapshot.selected : kInvalidEntity;
    controllableEntity = snapshot.controllableEntity;
    gameCameraEntity = snapshot.gameCameraEntity;
}

void EditorContext::removeAnimationDemo() {
    for (Entity entity : animationDemoEntities) {
        if (world.isAlive(entity)) {
            world.destroyEntity(entity);
        }
        if (selected == entity) {
            selected = kInvalidEntity;
        }
    }
    animationDemoEntities.clear();
}

void EditorContext::rebuildAnimationDemo() {
    auto& resources = ResourceManager::getInstance();
    auto mesh = animationLoad ? animationLoad : resources.loadMeshAsync(animationPath.data());
    if (mesh && mesh->isPending()) {
        animationLoad = mesh;
        animationError = "Loading model in background...";
        return;
    }
    animationLoad.reset();
    if (!mesh || !mesh->isLoaded()) {
        animationError = "Import failed. Check the model path and engine log.";
        return;
    }
    const auto& data = *mesh->getData();
    const bool hasSkin = std::any_of(data.subMeshes.begin(), data.subMeshes.end(), [](const SubMesh& sub) { return !sub.bones.empty(); });
    if (!hasSkin || data.skeleton.clips.empty()) {
        animationError = "The model must contain skin weights, a skeleton and at least one animation clip.";
        return;
    }
    auto shader = resources.loadShader(kVertexShaderPath, kFragmentShaderPath);
    if (!shader) {
        animationError = "Skinning shader failed to compile. Check the engine log.";
        return;
    }
    // Вписываем геометрию позы привязки, а не произвольные единицы исходника, в персонажа высотой две единицы.
    Vec3 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 maximum{-minimum.x, -minimum.y, -minimum.z};
    AnimationPose bind;
    Animation::preparePose(data, bind);
    Animation::evaluate(data, std::numeric_limits<unsigned int>::max(), 0, bind);
    for (size_t s = 0; s < data.subMeshes.size(); ++s) {
        const auto& sub = data.subMeshes[s];
        for (const auto& vertex : sub.vertices) {
            Mat4 transform = Math::multiply(data.skeleton.rootInverse, bind.globals[sub.skeletonNode]);
            float total = 0;
            for (float w : vertex.boneWeights) total += w;
            if (!sub.bones.empty() && total > 0) {
                transform = {};
                for (size_t b = 0; b < 4; ++b) {
                    for (size_t k = 0; k < 16; ++k) {
                        transform.values[k] += bind.palettes[s][vertex.boneIds[b]].values[k] * vertex.boneWeights[b];
                    }
                }
            }
            const auto& m = transform.values;
            const auto& v = vertex.position;
            Vec3 p{m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12], m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13], m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14]};
            if (animationYUp) p = {p.x, -p.z, p.y};
            minimum = {std::min(minimum.x, p.x), std::min(minimum.y, p.y), std::min(minimum.z, p.z)};
            maximum = {std::max(maximum.x, p.x), std::max(maximum.y, p.y), std::max(maximum.z, p.z)};
        }
    }
    const float scale = 2.0f / std::max(0.001f, maximum.z - minimum.z);
    const float spacing = std::max(2.5f, (maximum.x - minimum.x) * scale * 1.4f);
    removeAnimationDemo();
    const int columns = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(animationDemoCount))));
    for (int i = 0; i < animationDemoCount; ++i) {
        const Entity entity = world.createEntity();
        animationDemoEntities.push_back(entity);
        world.addComponent<Tag>(entity, Tag{"Animated character " + std::to_string(i + 1)});
        Transform transform;
        transform.position = {20 + (i % columns - (columns - 1) * 0.5f) * spacing - (minimum.x + maximum.x) * 0.5f * scale,
            (i / columns - (columns - 1) * 0.5f) * spacing - (minimum.y + maximum.y) * 0.5f * scale, -minimum.z * scale};
        transform.scale = {scale, scale, scale};
        if (animationYUp) transform.rotation.x = 1.57079632679f;
        world.addComponent<Transform>(entity, transform);
        auto& meshRenderer = world.addComponent<MeshRenderer>(entity);
        meshRenderer.meshId = animationPath.data();
        meshRenderer.cachedMesh = mesh;
        meshRenderer.cachedShader = shader;
        meshRenderer.shaderId = ResourceManager::makeShaderKey(kVertexShaderPath, kFragmentShaderPath);
        auto& animator = world.addComponent<Animator>(entity);
        animator.time = data.skeleton.clips.front().duration * static_cast<double>(i) / animationDemoCount;
    }
    animationSystem.update(world, 0);
    selected = animationDemoEntities.front();
    selectedAsset.clear();
    camera.focus({20, 0, 1}, std::max(3.0f, columns * spacing * 0.65f));
    animationError.clear();
}
