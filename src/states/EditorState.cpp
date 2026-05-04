#include "states/EditorState.h"

#include "core/Logger.h"
#include "core/ServiceLocator.h"
#include "ecs/CollisionUtils.h"
#include "events/CollisionEvent.h"
#include "input/InputManager.h"
#include "input/KeyCode.h"
#include "render/IRenderAdapter.h"
#include "resources/HotReload.h"
#include "resources/ResourceManager.h"
#include "resources/SceneManifest.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>

namespace {
constexpr float kPi = 3.1415926f;
constexpr float kMoveSpeed = 1.0f;
constexpr float kJumpSpeed = 3.5f;
constexpr float kScaleStep = 0.1f;
constexpr float kRotationStep = kPi / 4.0f;
constexpr float kMinScale = 0.1f;
constexpr const char* kSceneManifestPath = "assets/scenes/demo_scene.json";
constexpr const char* kFallbackMeshPath = "primitive:cube";
constexpr const char* kFallbackTexturePath = "assets/textures/WoodCrate02.dds";
constexpr const char* kFallbackVertexShaderPath = "assets/shaders/mesh_vertex.glsl";
constexpr const char* kFallbackFragmentShaderPath = "assets/shaders/mesh_fragment_simple_texture.glsl";

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

Mat4 buildViewMatrix(const Transform& transform) {
    const float cosPitch = std::cos(transform.rotation.x);
    const Vec3 forward{
        std::sin(transform.rotation.y) * cosPitch,
        std::sin(transform.rotation.x),
        -std::cos(transform.rotation.y) * cosPitch
    };
    const Vec3 right{std::cos(transform.rotation.y), 0.0f, std::sin(transform.rotation.y)};
    const Vec3 up{
        right.y * forward.z - right.z * forward.y,
        right.z * forward.x - right.x * forward.z,
        right.x * forward.y - right.y * forward.x
    };

    Mat4 view = Mat4::identity();
    view.values[0] = right.x;
    view.values[1] = up.x;
    view.values[2] = -forward.x;
    view.values[4] = right.y;
    view.values[5] = up.y;
    view.values[6] = -forward.y;
    view.values[8] = right.z;
    view.values[9] = up.z;
    view.values[10] = -forward.z;
    view.values[12] = -(right.x * transform.position.x + right.y * transform.position.y + right.z * transform.position.z);
    view.values[13] = -(up.x * transform.position.x + up.y * transform.position.y + up.z * transform.position.z);
    view.values[14] = forward.x * transform.position.x + forward.y * transform.position.y + forward.z * transform.position.z;
    return view;
}

float toDegrees(float radians) {
    return radians * 180.0f / kPi;
}

float toRadians(float degrees) {
    return degrees * kPi / 180.0f;
}

float nearestEquivalentAngle(float candidate, float reference) {
    constexpr float twoPi = kPi * 2.0f;
    while (candidate - reference > kPi) {
        candidate -= twoPi;
    }
    while (candidate - reference < -kPi) {
        candidate += twoPi;
    }
    return candidate;
}

Vec3 nearestEquivalentEuler(const Vec3& candidate, const Vec3& reference) {
    return Vec3{
        nearestEquivalentAngle(candidate.x, reference.x),
        nearestEquivalentAngle(candidate.y, reference.y),
        nearestEquivalentAngle(candidate.z, reference.z)
    };
}

std::string formatBytes(std::size_t bytes) {
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024.0) {
        return std::to_string(static_cast<int>(kb)) + " KB";
    }

    const double mb = kb / 1024.0;
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.2f MB", mb);
    return buffer;
}

void copyStringToBuffer(const std::string& source, char* buffer, std::size_t bufferSize) {
    if (bufferSize == 0) {
        return;
    }

    std::strncpy(buffer, source.c_str(), bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
}

bool splitShaderKey(const std::string& key, std::string& vertexPath, std::string& fragmentPath) {
    const std::size_t separator = key.find('|');
    if (separator == std::string::npos) {
        return false;
    }

    vertexPath = key.substr(0, separator);
    fragmentPath = key.substr(separator + 1);
    return true;
}

bool rayIntersectsAABB(const Vec3& origin, const Vec3& direction, const AABB& aabb, float& outDistance) {
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();

    auto testAxis = [&](float originValue, float directionValue, float minValue, float maxValue) {
        if (std::abs(directionValue) < 0.00001f) {
            return originValue >= minValue && originValue <= maxValue;
        }

        float first = (minValue - originValue) / directionValue;
        float second = (maxValue - originValue) / directionValue;
        if (first > second) {
            std::swap(first, second);
        }

        tMin = std::max(tMin, first);
        tMax = std::min(tMax, second);
        return tMin <= tMax;
    };

    if (!testAxis(origin.x, direction.x, aabb.center.x - aabb.halfSize.x, aabb.center.x + aabb.halfSize.x)) {
        return false;
    }
    if (!testAxis(origin.y, direction.y, aabb.center.y - aabb.halfSize.y, aabb.center.y + aabb.halfSize.y)) {
        return false;
    }
    if (!testAxis(origin.z, direction.z, aabb.center.z - aabb.halfSize.z, aabb.center.z + aabb.halfSize.z)) {
        return false;
    }

    outDistance = tMin;
    return tMax >= 0.0f;
}

AABB buildPickBounds(const Transform& transform) {
    return AABB{
        transform.position,
        Vec3{
            std::max(std::abs(transform.scale.x) * 0.5f, 0.15f),
            std::max(std::abs(transform.scale.y) * 0.5f, 0.15f),
            std::max(std::abs(transform.scale.z) * 0.5f, 0.15f)
        }
    };
}

Vec3 addVec3(const Vec3& left, const Vec3& right) {
    return Vec3{left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 scaleVec3(const Vec3& value, float scalar) {
    return Vec3{value.x * scalar, value.y * scalar, value.z * scalar};
}
}

EditorState::EditorState(IRenderAdapter& renderer)
    : renderer_(renderer),
      physicsSystem_(),
      renderSystem_(renderer),
      debugRenderSystem_(renderer) {
}

void EditorState::onEnter() {
    LOG_INFO("EditorState: entered");
    bindActions();
    createScene();
    createGameCamera();
    createEditorCameraEntity();
    setCameraMode();
    debugRenderSystem_.setEnabled(false);

    ServiceLocator::getEventDispatcher().clear();
    ServiceLocator::getEventDispatcher().addListener<CollisionEvent>([this](const CollisionEvent& event) {
        LOG_INFO(
            "CollisionEvent: " +
            entityLabel(event.first) +
            " <-> " +
            entityLabel(event.second) +
            ", penetration=" +
            std::to_string(event.penetration));
    });
}

void EditorState::onExit() {
    LOG_INFO("EditorState: exited");
    ServiceLocator::getEventDispatcher().clear();
    world_.clear();
    selectedEntity_ = kInvalidEntity;
    controllableEntity_ = kInvalidEntity;
    gameCameraEntity_ = kInvalidEntity;
    editorCameraEntity_ = kInvalidEntity;
}

void EditorState::update(float dt) {
    lastDt_ = dt;
    fpsAccumulator_ += dt;
    ++fpsFrames_;
    if (fpsAccumulator_ >= 1.0f) {
        fpsAverage_ = static_cast<float>(fpsFrames_) / fpsAccumulator_;
        fpsAccumulator_ = 0.0f;
        fpsFrames_ = 0;
    }

    if (HotReload::getInstance().update()) {
        const auto& changedFiles = HotReload::getInstance().getChangedFiles();
        for (const auto& path : changedFiles) {
            ResourceManager::getInstance().reloadShadersForFile(path);
        }
    }

    const ImGuiIO& io = ImGui::GetIO();
    const bool allowGameInput = viewportInputActive_ && !io.WantTextInput;

    if (mode_ == EditorMode::Play) {
        updateGameplay(dt, allowGameInput);
    }
}

void EditorState::render() {
    renderDockSpace();
    renderMainMenu();
    renderToolbar();

    const ImGuiIO& io = ImGui::GetIO();
    if (mode_ == EditorMode::Edit && !io.WantTextInput && world_.isAlive(selectedEntity_) && !isEditorEntity(selectedEntity_)) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
            duplicateEntity(selectedEntity_);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
            beginRename(selectedEntity_);
        }
    }

    if (showHierarchy_) {
        renderHierarchyPanel();
    }
    if (showInspector_) {
        renderInspectorPanel();
    }
    if (showStatistics_) {
        renderStatisticsPanel();
    }
    if (showViewport_) {
        renderViewportPanel();
    }
}

void EditorState::bindActions() {
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

void EditorState::createScene() {
    world_.clear();
    selectedEntity_ = kInvalidEntity;
    controllableEntity_ = kInvalidEntity;
    gameCameraEntity_ = kInvalidEntity;
    editorCameraEntity_ = kInvalidEntity;

    if (!createSceneFromManifest()) {
        createFallbackScene();
    }

    for (Entity entity : world_.getEntities()) {
        if (!isEditorEntity(entity)) {
            selectedEntity_ = entity;
            break;
        }
    }
}

bool EditorState::createSceneFromManifest() {
    SceneManifest manifest;
    if (!manifest.loadFromFile(kSceneManifestPath)) {
        return false;
    }

    auto& resourceManager = ResourceManager::getInstance();

    for (const SceneEntityDescription& description : manifest.getEntities()) {
        const std::string* meshPath = manifest.findMeshPath(description.meshId);
        const ShaderAssetPaths* shaderPaths = manifest.findShader(description.shaderId);
        if (meshPath == nullptr || shaderPaths == nullptr) {
            LOG_ERROR("Scene entity has unresolved resources: " + description.tag);
            continue;
        }

        MeshRenderer renderer;
        renderer.meshId = description.meshId;
        renderer.baseColorTextureId = description.baseColorTextureId;
        renderer.shaderId = description.shaderId;
        renderer.cachedMesh = resourceManager.load<MeshData>(*meshPath);
        renderer.cachedShader = resourceManager.loadShader(shaderPaths->vertexPath, shaderPaths->fragmentPath);

        if (!description.baseColorTextureId.empty()) {
            if (const std::string* texturePath = manifest.findTexturePath(description.baseColorTextureId)) {
                renderer.cachedBaseColorTexture = resourceManager.load<TextureData>(*texturePath);
            }
        }

        if (!renderer.cachedMesh || !renderer.cachedShader) {
            LOG_ERROR("Failed to load scene resources for entity: " + description.tag);
            continue;
        }

        Entity entity = world_.createEntity();
        world_.addComponent<Tag>(entity, Tag{description.tag});
        world_.addComponent<Transform>(entity, Transform{description.position, description.rotation, description.scale});
        world_.addComponent<MeshRenderer>(entity, renderer);
        if (description.hasRigidbody) {
            world_.addComponent<Rigidbody>(entity, description.rigidbody);
        }
        if (description.hasCollider) {
            Collider collider = description.collider;
            fitColliderToMeshBounds(renderer, collider);
            world_.addComponent<Collider>(entity, collider);
        }
        if (description.spinSpeed != 0.0f) {
            world_.addComponent<Spin>(entity, Spin{description.spinSpeed});
        }

        if (description.controllable) {
            controllableEntity_ = entity;
            if (!world_.hasComponent<Rigidbody>(entity)) {
                world_.addComponent<Rigidbody>(entity, Rigidbody{Vec3{}, Vec3{}, 1.0f, true});
            }
            if (!world_.hasComponent<Collider>(entity)) {
                Collider collider{ColliderType::Box, Vec3{0.5f, 0.5f, 0.5f}, Vec3{}, 0.5f};
                fitColliderToMeshBounds(renderer, collider);
                world_.addComponent<Collider>(entity, collider);
            }
        }

        HotReload::getInstance().watchFile(shaderPaths->vertexPath);
        HotReload::getInstance().watchFile(shaderPaths->fragmentPath);
    }

    return !world_.getEntities().empty();
}

void EditorState::createFallbackScene() {
    LOG_ERROR("EditorState: falling back to resource-based test scene");

    auto& resourceManager = ResourceManager::getInstance();
    MeshRenderer renderer;
    renderer.meshId = "fallback_cube";
    renderer.baseColorTextureId = "fallback_crate";
    renderer.shaderId = "fallback_textured";
    renderer.cachedMesh = resourceManager.load<MeshData>(kFallbackMeshPath);
    renderer.cachedBaseColorTexture = resourceManager.load<TextureData>(kFallbackTexturePath);
    renderer.cachedShader = resourceManager.loadShader(kFallbackVertexShaderPath, kFallbackFragmentShaderPath);

    if (!renderer.cachedMesh || !renderer.cachedShader || !renderer.cachedBaseColorTexture) {
        LOG_ERROR("EditorState: failed to build fallback scene resources");
        return;
    }

    HotReload::getInstance().watchFile(kFallbackVertexShaderPath);
    HotReload::getInstance().watchFile(kFallbackFragmentShaderPath);

    controllableEntity_ = world_.createEntity();
    world_.addComponent<Tag>(controllableEntity_, Tag{"FallbackCube"});
    world_.addComponent<Transform>(controllableEntity_, Transform{{0.0f, 0.0f, 0.0f}, {}, {0.8f, 0.8f, 0.8f}});
    world_.addComponent<MeshRenderer>(controllableEntity_, renderer);
    world_.addComponent<Rigidbody>(controllableEntity_, Rigidbody{Vec3{}, Vec3{}, 1.0f, true});
    Collider collider{ColliderType::Box, Vec3{0.5f, 0.5f, 0.5f}, Vec3{}, 0.5f};
    fitColliderToMeshBounds(renderer, collider);
    world_.addComponent<Collider>(controllableEntity_, collider);
}

Entity EditorState::createCubeEntity(const std::string& requestedName, const Vec3& position) {
    ResourceManager& resourceManager = ResourceManager::getInstance();

    MeshRenderer renderer;
    renderer.meshId = kFallbackMeshPath;
    renderer.baseColorTextureId = kFallbackTexturePath;
    renderer.shaderId = std::string(kFallbackVertexShaderPath) + "|" + kFallbackFragmentShaderPath;
    renderer.cachedMesh = resourceManager.load<MeshData>(kFallbackMeshPath);
    renderer.cachedBaseColorTexture = resourceManager.load<TextureData>(kFallbackTexturePath);
    renderer.cachedShader = resourceManager.loadShader(kFallbackVertexShaderPath, kFallbackFragmentShaderPath);

    if (!renderer.cachedMesh || !renderer.cachedBaseColorTexture || !renderer.cachedShader) {
        LOG_ERROR("EditorState: failed to create cube entity resources");
        return kInvalidEntity;
    }

    Entity entity = world_.createEntity();
    world_.addComponent<Tag>(entity, Tag{requestedName});
    world_.addComponent<Transform>(entity, Transform{position, {}, {1.0f, 1.0f, 1.0f}});
    world_.addComponent<MeshRenderer>(entity, renderer);
    Collider collider{ColliderType::Box, Vec3{0.5f, 0.5f, 0.5f}, Vec3{}, 0.5f};
    fitColliderToMeshBounds(renderer, collider);
    world_.addComponent<Collider>(entity, collider);

    HotReload::getInstance().watchFile(kFallbackVertexShaderPath);
    HotReload::getInstance().watchFile(kFallbackFragmentShaderPath);

    selectedEntity_ = entity;
    return entity;
}

Entity EditorState::duplicateEntity(Entity source) {
    if (!world_.isAlive(source) || isEditorEntity(source)) {
        return kInvalidEntity;
    }

    Entity entity = world_.createEntity();

    if (world_.hasComponent<Tag>(source)) {
        world_.addComponent<Tag>(entity, Tag{world_.getComponent<Tag>(source).name + " Copy"});
    } else {
        world_.addComponent<Tag>(entity, Tag{"Entity Copy"});
    }

    if (world_.hasComponent<Transform>(source)) {
        Transform transform = world_.getComponent<Transform>(source);
        transform.position.x += 1.0f;
        transform.position.z += 1.0f;
        world_.addComponent<Transform>(entity, transform);
    }
    if (world_.hasComponent<MeshRenderer>(source)) {
        world_.addComponent<MeshRenderer>(entity, world_.getComponent<MeshRenderer>(source));
    }
    if (world_.hasComponent<Rigidbody>(source)) {
        Rigidbody rigidbody = world_.getComponent<Rigidbody>(source);
        rigidbody.velocity = Vec3{};
        rigidbody.acceleration = Vec3{};
        world_.addComponent<Rigidbody>(entity, rigidbody);
    }
    if (world_.hasComponent<Collider>(source)) {
        world_.addComponent<Collider>(entity, world_.getComponent<Collider>(source));
    }
    if (world_.hasComponent<Spin>(source)) {
        world_.addComponent<Spin>(entity, world_.getComponent<Spin>(source));
    }
    if (world_.hasComponent<Camera>(source)) {
        Camera camera = world_.getComponent<Camera>(source);
        camera.active = false;
        world_.addComponent<Camera>(entity, camera);
    }

    selectedEntity_ = entity;
    return entity;
}

void EditorState::beginRename(Entity entity) {
    if (!world_.isAlive(entity) || isEditorEntity(entity)) {
        return;
    }

    renamingEntity_ = entity;
    renameBuffer_.fill('\0');
    copyStringToBuffer(entityLabel(entity), renameBuffer_.data(), renameBuffer_.size());
    if (world_.hasComponent<Tag>(entity)) {
        copyStringToBuffer(world_.getComponent<Tag>(entity).name, renameBuffer_.data(), renameBuffer_.size());
    }
}

void EditorState::commitRename() {
    if (!world_.isAlive(renamingEntity_) || isEditorEntity(renamingEntity_)) {
        renamingEntity_ = kInvalidEntity;
        return;
    }

    if (!world_.hasComponent<Tag>(renamingEntity_)) {
        world_.addComponent<Tag>(renamingEntity_, Tag{});
    }
    world_.getComponent<Tag>(renamingEntity_).name = renameBuffer_.data();
    renamingEntity_ = kInvalidEntity;
}

Vec3 EditorState::defaultSpawnPosition() const {
    return addVec3(editorCamera_.getPosition(), scaleVec3(editorCamera_.getForward(), 4.0f));
}

void EditorState::createGameCamera() {
    gameCameraEntity_ = world_.createEntity();
    world_.addComponent<Tag>(gameCameraEntity_, Tag{"MainCamera"});
    world_.addComponent<Transform>(gameCameraEntity_, Transform{
        Vec3{0.0f, 4.0f, 10.0f},
        Vec3{-0.3f, 0.0f, 0.0f},
        Vec3{1.0f, 1.0f, 1.0f}
    });
    world_.addComponent<Camera>(gameCameraEntity_, Camera{45.0f, 0.1f, 100.0f, 800.0f / 600.0f, false, Mat4::identity(), Mat4::identity()});
}

void EditorState::createEditorCameraEntity() {
    editorCameraEntity_ = world_.createEntity();
    world_.addComponent<Transform>(editorCameraEntity_, Transform{});
    world_.addComponent<Camera>(editorCameraEntity_, Camera{45.0f, 0.1f, 200.0f, 800.0f / 600.0f, true, Mat4::identity(), Mat4::identity()});
    syncEditorCameraEntity();
}

void EditorState::syncEditorCameraEntity() {
    if (!world_.isAlive(editorCameraEntity_) || !world_.hasComponent<Camera>(editorCameraEntity_)) {
        return;
    }

    Camera& camera = world_.getComponent<Camera>(editorCameraEntity_);
    camera.active = mode_ == EditorMode::Edit;
    camera.aspectRatio = viewportHeight_ > 0 ? static_cast<float>(viewportWidth_) / static_cast<float>(viewportHeight_) : 800.0f / 600.0f;
    camera.viewMatrix = editorCamera_.getViewMatrix();
    camera.projectionMatrix = editorCamera_.getProjectionMatrix();
    if (world_.hasComponent<Transform>(editorCameraEntity_)) {
        world_.getComponent<Transform>(editorCameraEntity_).position = editorCamera_.getPosition();
    }
}

void EditorState::setCameraMode() {
    if (world_.isAlive(editorCameraEntity_) && world_.hasComponent<Camera>(editorCameraEntity_)) {
        world_.getComponent<Camera>(editorCameraEntity_).active = mode_ == EditorMode::Edit;
    }
    if (world_.isAlive(gameCameraEntity_) && world_.hasComponent<Camera>(gameCameraEntity_)) {
        world_.getComponent<Camera>(gameCameraEntity_).active = mode_ == EditorMode::Play;
    }
}

void EditorState::updateGameplay(float dt, bool allowInput) {
    if (allowInput) {
        processGameplayInput(dt);
    }

    physicsSystem_.update(world_, dt);
    spinSystem_.update(world_, dt);
    updateGameCamera(dt, allowInput);
}

void EditorState::updateGameCamera(float dt, bool allowInput) {
    if (!world_.isAlive(gameCameraEntity_) ||
        !world_.hasComponent<Transform>(gameCameraEntity_) ||
        !world_.hasComponent<Camera>(gameCameraEntity_)) {
        return;
    }

    Transform& transform = world_.getComponent<Transform>(gameCameraEntity_);
    Camera& camera = world_.getComponent<Camera>(gameCameraEntity_);

    if (allowInput) {
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

        const float yaw = transform.rotation.y;
        const float forwardX = std::sin(yaw);
        const float forwardZ = -std::cos(yaw);
        const float rightX = std::cos(yaw);
        const float rightZ = std::sin(yaw);
        constexpr float cameraMoveSpeed = 3.5f;
        transform.position.x += (forwardX * moveForward + rightX * moveRight) * cameraMoveSpeed * dt;
        transform.position.y += moveUp * cameraMoveSpeed * dt;
        transform.position.z += (forwardZ * moveForward + rightZ * moveRight) * cameraMoveSpeed * dt;

        if (inputManager.isMouseButtonDown(KeyCode::MouseRight)) {
            const Vec2 mouseDelta = inputManager.getMouseDelta();
            transform.rotation.y += mouseDelta.x * 0.003f;
            transform.rotation.x -= mouseDelta.y * 0.003f;
            transform.rotation.x = std::clamp(transform.rotation.x, -1.4f, 1.4f);
        }
    }

    camera.active = mode_ == EditorMode::Play;
    camera.aspectRatio = viewportHeight_ > 0 ? static_cast<float>(viewportWidth_) / static_cast<float>(viewportHeight_) : 800.0f / 600.0f;
    camera.viewMatrix = buildViewMatrix(transform);
    camera.projectionMatrix = Math::perspective(camera.fovDegrees * kPi / 180.0f, camera.aspectRatio, camera.nearClip, camera.farClip);
}

void EditorState::processGameplayInput(float) {
    if (!world_.isAlive(controllableEntity_) || !world_.hasComponent<Rigidbody>(controllableEntity_)) {
        return;
    }

    Transform& transform = world_.getComponent<Transform>(controllableEntity_);
    Rigidbody& rigidbody = world_.getComponent<Rigidbody>(controllableEntity_);
    InputManager& inputManager = InputManager::getInstance();

    float horizontalVelocity = 0.0f;
    float depthVelocity = 0.0f;

    if (inputManager.isActionDown("MoveLeft")) horizontalVelocity -= kMoveSpeed;
    if (inputManager.isActionDown("MoveRight")) horizontalVelocity += kMoveSpeed;
    if (inputManager.isActionDown("MoveForward")) depthVelocity -= kMoveSpeed;
    if (inputManager.isActionDown("MoveBackward")) depthVelocity += kMoveSpeed;

    rigidbody.velocity.x = horizontalVelocity;
    rigidbody.velocity.z = depthVelocity;

    if (inputManager.isActionPressed("Jump") && std::abs(transform.position.y) < 0.051f) {
        rigidbody.velocity.y = kJumpSpeed;
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
        transform.rotation.y += kRotationStep;
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

void EditorState::renderDockSpace() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("EditorDockSpaceHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    const ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");
    buildDefaultDockLayout(dockspaceId);
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}

void EditorState::buildDefaultDockLayout(unsigned int dockspaceId) {
    if (dockLayoutBuilt_) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

    ImGuiID root = dockspaceId;
    ImGuiID toolbarNode = 0;
    ImGuiID leftNode = 0;
    ImGuiID rightNode = 0;
    ImGuiID bottomNode = 0;
    ImGuiID centerNode = 0;

    ImGui::DockBuilderSplitNode(root, ImGuiDir_Up, 0.075f, &toolbarNode, &root);
    ImGui::DockBuilderSplitNode(root, ImGuiDir_Left, 0.20f, &leftNode, &root);
    ImGui::DockBuilderSplitNode(root, ImGuiDir_Right, 0.28f, &rightNode, &root);
    ImGui::DockBuilderSplitNode(root, ImGuiDir_Down, 0.22f, &bottomNode, &centerNode);

    ImGui::DockBuilderDockWindow("Toolbar", toolbarNode);
    ImGui::DockBuilderDockWindow("Scene Hierarchy", leftNode);
    ImGui::DockBuilderDockWindow("Inspector", rightNode);
    ImGui::DockBuilderDockWindow("Statistics", bottomNode);
    ImGui::DockBuilderDockWindow("Viewport", centerNode);
    ImGui::DockBuilderFinish(dockspaceId);
    dockLayoutBuilt_ = true;
}

void EditorState::renderMainMenu() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Reload Scene")) {
            if (mode_ == EditorMode::Play) {
                stopPlayMode();
            }
            createScene();
            createGameCamera();
            createEditorCameraEntity();
            setCameraMode();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Scene")) {
        ImGui::BeginDisabled(mode_ == EditorMode::Play);
        if (ImGui::MenuItem("Create Cube")) {
            createCubeEntity("Cube", defaultSpawnPosition());
        }
        if (ImGui::MenuItem("Duplicate Selected", "Ctrl+D", false, world_.isAlive(selectedEntity_) && !isEditorEntity(selectedEntity_))) {
            duplicateEntity(selectedEntity_);
        }
        if (ImGui::MenuItem("Rename Selected", "F2", false, world_.isAlive(selectedEntity_) && !isEditorEntity(selectedEntity_))) {
            beginRename(selectedEntity_);
        }
        ImGui::EndDisabled();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Scene Hierarchy", nullptr, &showHierarchy_);
        ImGui::MenuItem("Inspector", nullptr, &showInspector_);
        ImGui::MenuItem("Viewport", nullptr, &showViewport_);
        ImGui::MenuItem("Statistics", nullptr, &showStatistics_);
        ImGui::Separator();
        bool debugEnabled = debugRenderSystem_.isEnabled();
        if (ImGui::MenuItem("Debug Colliders", nullptr, &debugEnabled)) {
            debugRenderSystem_.setEnabled(debugEnabled);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        ImGui::TextUnformatted("ITMO Uzlezz Engine Editor");
        ImGui::TextUnformatted("RMB+WASD fly, MMB pan, Alt+LMB orbit, F focus.");
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void EditorState::renderToolbar() {
    ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const bool playing = mode_ == EditorMode::Play;
    ImGui::BeginDisabled(playing);
    if (ImGui::Button("Create Cube")) {
        createCubeEntity("Cube", defaultSpawnPosition());
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate") && world_.isAlive(selectedEntity_) && !isEditorEntity(selectedEntity_)) {
        duplicateEntity(selectedEntity_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Rename") && world_.isAlive(selectedEntity_) && !isEditorEntity(selectedEntity_)) {
        beginRename(selectedEntity_);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();

    if (!playing) {
        if (ImGui::Button("Play")) {
            startPlayMode();
        }
    } else {
        if (ImGui::Button("Stop")) {
            stopPlayMode();
        }
    }

    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();

    ImGui::BeginDisabled(playing);
    if (ImGui::Selectable("Translate", gizmoOperation_ == GizmoOperation::Translate, 0, ImVec2(82.0f, 0.0f))) {
        gizmoOperation_ = GizmoOperation::Translate;
    }
    ImGui::SameLine();
    if (ImGui::Selectable("Rotate", gizmoOperation_ == GizmoOperation::Rotate, 0, ImVec2(64.0f, 0.0f))) {
        gizmoOperation_ = GizmoOperation::Rotate;
    }
    ImGui::SameLine();
    if (ImGui::Selectable("Scale", gizmoOperation_ == GizmoOperation::Scale, 0, ImVec2(58.0f, 0.0f))) {
        gizmoOperation_ = GizmoOperation::Scale;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Local", &gizmoLocalMode_);
    ImGui::EndDisabled();

    ImGui::SameLine();
    bool debugEnabled = debugRenderSystem_.isEnabled();
    if (ImGui::Checkbox("Colliders", &debugEnabled)) {
        debugRenderSystem_.setEnabled(debugEnabled);
    }

    ImGui::End();
}

void EditorState::renderHierarchyPanel() {
    ImGui::Begin("Scene Hierarchy", &showHierarchy_);

    ImGui::BeginDisabled(mode_ == EditorMode::Play);
    if (ImGui::Button("Create Cube")) {
        createCubeEntity("Cube", defaultSpawnPosition());
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate") && world_.isAlive(selectedEntity_) && !isEditorEntity(selectedEntity_)) {
        duplicateEntity(selectedEntity_);
    }
    ImGui::EndDisabled();
    ImGui::Separator();

    std::unordered_set<Entity> childEntities;
    for (Entity entity : world_.getEntities()) {
        if (isEditorEntity(entity) || !world_.hasComponent<Hierarchy>(entity)) {
            continue;
        }
        for (Entity child : world_.getComponent<Hierarchy>(entity).children) {
            childEntities.insert(child);
        }
    }

    for (Entity entity : world_.getEntities()) {
        if (isEditorEntity(entity) || childEntities.find(entity) != childEntities.end()) {
            continue;
        }
        renderHierarchyEntity(entity);
    }

    if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        ImGui::BeginDisabled(mode_ == EditorMode::Play);
        if (ImGui::MenuItem("Create Cube")) {
            createCubeEntity("Cube", defaultSpawnPosition());
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    ImGui::End();
}

void EditorState::renderHierarchyEntity(Entity entity) {
    if (!world_.isAlive(entity) || isEditorEntity(entity)) {
        return;
    }

    bool hasChildren = false;
    if (world_.hasComponent<Hierarchy>(entity)) {
        for (Entity child : world_.getComponent<Hierarchy>(entity).children) {
            if (world_.isAlive(child) && !isEditorEntity(child)) {
                hasChildren = true;
                break;
            }
        }
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (selectedEntity_ == entity) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool isRenaming = renamingEntity_ == entity;
    const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(entity)), flags, "%s", isRenaming ? "" : entityLabel(entity).c_str());
    if (ImGui::IsItemClicked()) {
        selectedEntity_ = entity;
    }

    if (isRenaming) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SetKeyboardFocusHere();
        const bool submitted = ImGui::InputText("##RenameEntity", renameBuffer_.data(), renameBuffer_.size(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (submitted || (ImGui::IsItemDeactivatedAfterEdit() && !ImGui::IsItemActive())) {
            commitRename();
        }
    }

    if (ImGui::BeginPopupContextItem()) {
        selectedEntity_ = entity;
        ImGui::BeginDisabled(mode_ == EditorMode::Play);
        if (ImGui::MenuItem("Rename", "F2")) {
            beginRename(entity);
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
            duplicateEntity(entity);
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    if (open && hasChildren) {
        const Hierarchy& hierarchy = world_.getComponent<Hierarchy>(entity);
        for (Entity child : hierarchy.children) {
            renderHierarchyEntity(child);
        }
        ImGui::TreePop();
    }
}

void EditorState::renderInspectorPanel() {
    ImGui::Begin("Inspector", &showInspector_);

    if (!world_.isAlive(selectedEntity_)) {
        ImGui::TextUnformatted("No entity selected");
        ImGui::End();
        return;
    }

    ImGui::Text("Entity #%u", selectedEntity_);
    ImGui::Separator();

    const bool readOnly = mode_ == EditorMode::Play;
    ImGui::BeginDisabled(readOnly);

    if (world_.hasComponent<Tag>(selectedEntity_) && ImGui::TreeNodeEx("Tag", ImGuiTreeNodeFlags_DefaultOpen)) {
        Tag& tag = world_.getComponent<Tag>(selectedEntity_);
        char buffer[128] = {};
        copyStringToBuffer(tag.name, buffer, sizeof(buffer));
        if (ImGui::InputText("Name", buffer, sizeof(buffer))) {
            tag.name = buffer;
        }
        ImGui::TreePop();
    }

    if (world_.hasComponent<Transform>(selectedEntity_) && ImGui::TreeNodeEx("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        Transform& transform = world_.getComponent<Transform>(selectedEntity_);
        ImGui::DragFloat3("Position", &transform.position.x, 0.05f);
        float rotationDegrees[3] = {
            toDegrees(transform.rotation.x),
            toDegrees(transform.rotation.y),
            toDegrees(transform.rotation.z)
        };
        if (ImGui::DragFloat3("Rotation", rotationDegrees, 0.5f)) {
            transform.rotation.x = toRadians(rotationDegrees[0]);
            transform.rotation.y = toRadians(rotationDegrees[1]);
            transform.rotation.z = toRadians(rotationDegrees[2]);
        }
        if (ImGui::DragFloat3("Scale", &transform.scale.x, 0.05f, 0.01f, 100.0f)) {
            transform.scale.x = std::max(0.01f, transform.scale.x);
            transform.scale.y = std::max(0.01f, transform.scale.y);
            transform.scale.z = std::max(0.01f, transform.scale.z);
        }
        ImGui::TreePop();
    }

    if (world_.hasComponent<MeshRenderer>(selectedEntity_) && ImGui::TreeNodeEx("MeshRenderer", ImGuiTreeNodeFlags_DefaultOpen)) {
        MeshRenderer& meshRenderer = world_.getComponent<MeshRenderer>(selectedEntity_);
        ResourceManager& resources = ResourceManager::getInstance();

        const auto meshIds = resources.getMeshIds();
        if (ImGui::BeginCombo("Mesh", meshRenderer.meshId.empty() ? "<none>" : meshRenderer.meshId.c_str())) {
            for (const std::string& id : meshIds) {
                const bool selected = meshRenderer.meshId == id;
                if (ImGui::Selectable(id.c_str(), selected)) {
                    meshRenderer.meshId = id;
                    meshRenderer.cachedMesh = resources.load<MeshData>(id);
                    if (world_.hasComponent<Collider>(selectedEntity_)) {
                        fitColliderToMeshBounds(meshRenderer, world_.getComponent<Collider>(selectedEntity_));
                    }
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        const auto textureIds = resources.getTextureIds();
        if (ImGui::BeginCombo("Base Texture", meshRenderer.baseColorTextureId.empty() ? "<none>" : meshRenderer.baseColorTextureId.c_str())) {
            if (ImGui::Selectable("<none>", meshRenderer.baseColorTextureId.empty())) {
                meshRenderer.baseColorTextureId.clear();
                meshRenderer.cachedBaseColorTexture.reset();
            }
            for (const std::string& id : textureIds) {
                const bool selected = meshRenderer.baseColorTextureId == id;
                if (ImGui::Selectable(id.c_str(), selected)) {
                    meshRenderer.baseColorTextureId = id;
                    meshRenderer.cachedBaseColorTexture = resources.load<TextureData>(id);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        const auto shaderIds = resources.getShaderIds();
        if (ImGui::BeginCombo("Shader", meshRenderer.shaderId.empty() ? "<none>" : meshRenderer.shaderId.c_str())) {
            for (const std::string& id : shaderIds) {
                const bool selected = meshRenderer.shaderId == id;
                if (ImGui::Selectable(id.c_str(), selected)) {
                    std::string vertexPath;
                    std::string fragmentPath;
                    if (splitShaderKey(id, vertexPath, fragmentPath)) {
                        meshRenderer.shaderId = id;
                        meshRenderer.cachedShader = resources.loadShader(vertexPath, fragmentPath);
                    }
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::TreePop();
    }

    if (world_.hasComponent<Rigidbody>(selectedEntity_) && ImGui::TreeNodeEx("Rigidbody", ImGuiTreeNodeFlags_DefaultOpen)) {
        Rigidbody& rigidbody = world_.getComponent<Rigidbody>(selectedEntity_);
        ImGui::DragFloat("Mass", &rigidbody.mass, 0.05f, 0.0f, 1000.0f);
        ImGui::Checkbox("Use Gravity", &rigidbody.useGravity);
        ImGui::Text("Velocity: %.2f, %.2f, %.2f", rigidbody.velocity.x, rigidbody.velocity.y, rigidbody.velocity.z);
        ImGui::Text("Acceleration: %.2f, %.2f, %.2f", rigidbody.acceleration.x, rigidbody.acceleration.y, rigidbody.acceleration.z);
        ImGui::TreePop();
    }

    if (world_.hasComponent<Collider>(selectedEntity_) && ImGui::TreeNodeEx("Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
        Collider& collider = world_.getComponent<Collider>(selectedEntity_);
        int typeIndex = collider.type == ColliderType::Sphere ? 1 : 0;
        const char* types[] = {"Box", "Sphere"};
        if (ImGui::Combo("Type", &typeIndex, types, 2)) {
            collider.type = typeIndex == 1 ? ColliderType::Sphere : ColliderType::Box;
        }
        ImGui::DragFloat3("Offset", &collider.offset.x, 0.03f);
        if (collider.type == ColliderType::Box) {
            ImGui::DragFloat3("Half Extents", &collider.halfExtents.x, 0.03f, 0.01f, 100.0f);
        } else {
            ImGui::DragFloat("Radius", &collider.radius, 0.03f, 0.01f, 100.0f);
        }
        ImGui::TreePop();
    }

    ImGui::EndDisabled();

    if (readOnly) {
        ImGui::Separator();
        ImGui::TextUnformatted("Play mode is running. Stop to edit scene values.");
    }

    ImGui::End();
}

void EditorState::renderStatisticsPanel() {
    ImGui::Begin("Statistics", &showStatistics_);
    ResourceManager& resources = ResourceManager::getInstance();

    ImGui::Text("FPS: %.1f", fpsAverage_);
    ImGui::Text("Frame: %.2f ms", lastDt_ * 1000.0f);
    ImGui::Separator();
    ImGui::Text("Entities: %zu", world_.getEntityCount() - (world_.isAlive(editorCameraEntity_) ? 1u : 0u));
    ImGui::Text("Drawn meshes: %zu", renderSystem_.getLastDrawnMeshCount());
    ImGui::Text("Collisions: %zu", physicsSystem_.getLastCollisionCount());
    ImGui::Separator();
    ImGui::Text("Meshes: %zu", resources.getMeshCount());
    ImGui::Text("Textures: %zu", resources.getTextureCount());
    ImGui::Text("Shaders: %zu", resources.getShaderCount());
    ImGui::Text("Resource memory: %s", formatBytes(resources.estimateMemoryUsageBytes()).c_str());
    ImGui::End();
}

void EditorState::renderViewportPanel() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Viewport", &showViewport_, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    viewportWidth_ = std::max(1, static_cast<int>(contentSize.x));
    viewportHeight_ = std::max(1, static_cast<int>(contentSize.y));
    const ImVec2 viewportMin = ImGui::GetCursorScreenPos();
    const ImVec2 viewportMax = ImVec2(viewportMin.x + contentSize.x, viewportMin.y + contentSize.y);

    viewportHovered_ = ImGui::IsMouseHoveringRect(viewportMin, viewportMax, false);
    if (viewportHovered_ &&
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) {
        ImGui::SetWindowFocus();
    }
    viewportFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    const ImGuiIO& io = ImGui::GetIO();
    viewportInputActive_ = viewportHovered_ &&
        !io.WantTextInput &&
        !ImGuizmo::IsOver() &&
        !ImGuizmo::IsUsing();

    if (mode_ == EditorMode::Edit) {
        if (viewportInputActive_ &&
            InputManager::getInstance().isKeyPressed(KeyCode::F) &&
            world_.isAlive(selectedEntity_) &&
            world_.hasComponent<Transform>(selectedEntity_)) {
            const Transform& transform = world_.getComponent<Transform>(selectedEntity_);
            const float radius = std::max({transform.scale.x, transform.scale.y, transform.scale.z, 1.0f});
            editorCamera_.focus(transform.position, radius);
        }

        editorCamera_.update(lastDt_, viewportWidth_, viewportHeight_, viewportInputActive_, viewportHovered_ ? io.MouseWheel : 0.0f);
        syncEditorCameraEntity();
    }

    setCameraMode();
    renderViewportScene(viewportWidth_, viewportHeight_);

    const unsigned int textureId = renderer_.getViewportTextureId();
    ImGui::Image(
        reinterpret_cast<void*>(static_cast<intptr_t>(textureId)),
        contentSize,
        ImVec2(0.0f, 1.0f),
        ImVec2(1.0f, 0.0f));
    const bool viewportLeftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

    if (mode_ == EditorMode::Edit) {
        renderGizmo(viewportMin, contentSize);
        if (viewportLeftClicked && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
            selectEntityAtViewportPosition(viewportMin, contentSize);
        }
    }

    ImGui::End();
}

void EditorState::renderViewportScene(int width, int height) {
    renderer_.beginViewportFrame(width, height, 0.08f, 0.09f, 0.11f);
    renderSystem_.render(world_);
    debugRenderSystem_.render(world_);
    renderer_.endViewportFrame();
}

void EditorState::renderGizmo(const ImVec2& viewportMin, const ImVec2& viewportSize) {
    if (!world_.isAlive(selectedEntity_) || !world_.hasComponent<Transform>(selectedEntity_)) {
        return;
    }

    Transform& transform = world_.getComponent<Transform>(selectedEntity_);
    Mat4 model = Math::composeTransform(transform.position, transform.rotation, transform.scale);
    Mat4 deltaMatrix = Mat4::identity();
    const Mat4 view = activeViewMatrix();
    const Mat4 projection = activeProjectionMatrix();

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::AllowAxisFlip(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(viewportMin.x, viewportMin.y, viewportSize.x, viewportSize.y);

    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    if (gizmoOperation_ == GizmoOperation::Rotate) {
        operation = ImGuizmo::ROTATE;
    } else if (gizmoOperation_ == GizmoOperation::Scale) {
        operation = ImGuizmo::SCALE;
    }

    const ImGuizmo::MODE mode = gizmoLocalMode_ ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    if (ImGuizmo::Manipulate(view.data(), projection.data(), operation, mode, model.data(), deltaMatrix.data())) {
        float translation[3] = {};
        float rotation[3] = {};
        float scale[3] = {};
        ImGuizmo::DecomposeMatrixToComponents(model.data(), translation, rotation, scale);
        transform.position = Vec3{translation[0], translation[1], translation[2]};
        transform.scale = Vec3{scale[0], scale[1], scale[2]};

        if (gizmoOperation_ == GizmoOperation::Rotate) {
            float deltaTranslation[3] = {};
            float deltaRotation[3] = {};
            float deltaScale[3] = {};
            ImGuizmo::DecomposeMatrixToComponents(deltaMatrix.data(), deltaTranslation, deltaRotation, deltaScale);
            transform.rotation.x += toRadians(deltaRotation[0]);
            transform.rotation.y += toRadians(deltaRotation[1]);
            transform.rotation.z += toRadians(deltaRotation[2]);
        } else {
            transform.rotation = nearestEquivalentEuler(
                Vec3{toRadians(rotation[0]), toRadians(rotation[1]), toRadians(rotation[2])},
                transform.rotation);
        }
    }
}

void EditorState::selectEntityAtViewportPosition(const ImVec2& viewportMin, const ImVec2& viewportSize) {
    if (viewportSize.x <= 1.0f || viewportSize.y <= 1.0f) {
        return;
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float localX = mouse.x - viewportMin.x;
    const float localY = mouse.y - viewportMin.y;
    if (localX < 0.0f || localY < 0.0f || localX > viewportSize.x || localY > viewportSize.y) {
        return;
    }

    const float ndcX = (localX / viewportSize.x) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (localY / viewportSize.y) * 2.0f;
    const float aspect = viewportSize.y > 0.0f ? viewportSize.x / viewportSize.y : 1.0f;
    const Vec3 rayOrigin = editorCamera_.getPosition();
    const Vec3 rayDirection = editorCamera_.getRayDirection(ndcX, ndcY, aspect);

    Entity bestEntity = kInvalidEntity;
    float bestDistance = std::numeric_limits<float>::max();

    for (Entity entity : world_.getEntities()) {
        if (isEditorEntity(entity) || !world_.hasComponent<Transform>(entity)) {
            continue;
        }

        const Transform& transform = world_.getComponent<Transform>(entity);
        AABB bounds = buildPickBounds(transform);
        if (world_.hasComponent<Collider>(entity)) {
            const Collider& collider = world_.getComponent<Collider>(entity);
            if (collider.type == ColliderType::Box) {
                bounds = CollisionUtils::buildAABB(transform, collider);
            } else {
                bounds = AABB{
                    Vec3{
                        transform.position.x + collider.offset.x,
                        transform.position.y + collider.offset.y,
                        transform.position.z + collider.offset.z
                    },
                    Vec3{collider.radius, collider.radius, collider.radius}
                };
            }
        }

        float distance = 0.0f;
        if (rayIntersectsAABB(rayOrigin, rayDirection, bounds, distance) && distance < bestDistance) {
            bestDistance = distance;
            bestEntity = entity;
        }
    }

    if (bestEntity != kInvalidEntity) {
        selectedEntity_ = bestEntity;
    }
}

void EditorState::startPlayMode() {
    if (mode_ == EditorMode::Play) {
        return;
    }

    playSnapshot_ = captureSnapshot();
    mode_ = EditorMode::Play;
    setCameraMode();
    updateGameCamera(0.0f, false);
    lmbWasPressed_ = false;
    rmbWasPressed_ = false;
    mmbWasPressed_ = false;
}

void EditorState::stopPlayMode() {
    if (mode_ != EditorMode::Play) {
        return;
    }

    restoreSnapshot(playSnapshot_);
    mode_ = EditorMode::Edit;
    createEditorCameraEntity();
    setCameraMode();
}

EditorState::SceneSnapshot EditorState::captureSnapshot() const {
    SceneSnapshot snapshot;
    snapshot.selectedEntity = selectedEntity_;
    snapshot.controllableEntity = controllableEntity_;
    snapshot.gameCameraEntity = gameCameraEntity_;

    for (Entity entity : world_.getEntities()) {
        if (isEditorEntity(entity)) {
            continue;
        }

        EntitySnapshot entitySnapshot;
        entitySnapshot.entity = entity;
        if (world_.hasComponent<Tag>(entity)) {
            entitySnapshot.hasTag = true;
            entitySnapshot.tag = world_.getComponent<Tag>(entity);
        }
        if (world_.hasComponent<Transform>(entity)) {
            entitySnapshot.hasTransform = true;
            entitySnapshot.transform = world_.getComponent<Transform>(entity);
        }
        if (world_.hasComponent<MeshRenderer>(entity)) {
            entitySnapshot.hasMeshRenderer = true;
            entitySnapshot.meshRenderer = world_.getComponent<MeshRenderer>(entity);
        }
        if (world_.hasComponent<Hierarchy>(entity)) {
            entitySnapshot.hasHierarchy = true;
            entitySnapshot.hierarchy = world_.getComponent<Hierarchy>(entity);
        }
        if (world_.hasComponent<Spin>(entity)) {
            entitySnapshot.hasSpin = true;
            entitySnapshot.spin = world_.getComponent<Spin>(entity);
        }
        if (world_.hasComponent<Camera>(entity)) {
            entitySnapshot.hasCamera = true;
            entitySnapshot.camera = world_.getComponent<Camera>(entity);
        }
        if (world_.hasComponent<Rigidbody>(entity)) {
            entitySnapshot.hasRigidbody = true;
            entitySnapshot.rigidbody = world_.getComponent<Rigidbody>(entity);
        }
        if (world_.hasComponent<Collider>(entity)) {
            entitySnapshot.hasCollider = true;
            entitySnapshot.collider = world_.getComponent<Collider>(entity);
        }

        snapshot.entities.push_back(entitySnapshot);
    }

    return snapshot;
}

void EditorState::restoreSnapshot(const SceneSnapshot& snapshot) {
    world_.clear();
    editorCameraEntity_ = kInvalidEntity;

    for (const EntitySnapshot& entitySnapshot : snapshot.entities) {
        world_.createEntityWithId(entitySnapshot.entity);
    }

    for (const EntitySnapshot& entitySnapshot : snapshot.entities) {
        const Entity entity = entitySnapshot.entity;
        if (entitySnapshot.hasTag) world_.addComponent<Tag>(entity, entitySnapshot.tag);
        if (entitySnapshot.hasTransform) world_.addComponent<Transform>(entity, entitySnapshot.transform);
        if (entitySnapshot.hasMeshRenderer) world_.addComponent<MeshRenderer>(entity, entitySnapshot.meshRenderer);
        if (entitySnapshot.hasHierarchy) world_.addComponent<Hierarchy>(entity, entitySnapshot.hierarchy);
        if (entitySnapshot.hasSpin) world_.addComponent<Spin>(entity, entitySnapshot.spin);
        if (entitySnapshot.hasCamera) world_.addComponent<Camera>(entity, entitySnapshot.camera);
        if (entitySnapshot.hasRigidbody) world_.addComponent<Rigidbody>(entity, entitySnapshot.rigidbody);
        if (entitySnapshot.hasCollider) world_.addComponent<Collider>(entity, entitySnapshot.collider);
    }

    selectedEntity_ = snapshot.selectedEntity;
    controllableEntity_ = snapshot.controllableEntity;
    gameCameraEntity_ = snapshot.gameCameraEntity;
}

std::string EditorState::entityLabel(Entity entity) const {
    if (!world_.isAlive(entity)) {
        return "Entity#" + std::to_string(entity);
    }

    if (world_.hasComponent<Tag>(entity)) {
        const Tag& tag = world_.getComponent<Tag>(entity);
        if (!tag.name.empty()) {
            return tag.name + " (#" + std::to_string(entity) + ")";
        }
    }

    return "Entity#" + std::to_string(entity);
}

bool EditorState::isEditorEntity(Entity entity) const {
    return entity == editorCameraEntity_;
}

Mat4 EditorState::activeViewMatrix() const {
    if (mode_ == EditorMode::Play &&
        world_.isAlive(gameCameraEntity_) &&
        world_.hasComponent<Camera>(gameCameraEntity_)) {
        return world_.getComponent<Camera>(gameCameraEntity_).viewMatrix;
    }

    return editorCamera_.getViewMatrix();
}

Mat4 EditorState::activeProjectionMatrix() const {
    if (mode_ == EditorMode::Play &&
        world_.isAlive(gameCameraEntity_) &&
        world_.hasComponent<Camera>(gameCameraEntity_)) {
        return world_.getComponent<Camera>(gameCameraEntity_).projectionMatrix;
    }

    return editorCamera_.getProjectionMatrix();
}
