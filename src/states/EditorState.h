#pragma once

#include "editor/EditorCamera.h"
#include "ecs/Components.h"
#include "ecs/DebugRenderSystem.h"
#include "ecs/Entity.h"
#include "ecs/PhysicsSystem.h"
#include "ecs/RenderSystem.h"
#include "ecs/SpinSystem.h"
#include "ecs/World.h"
#include "states/IGameState.h"

#include <array>
#include <string>
#include <vector>

class IRenderAdapter;
struct ImVec2;

class EditorState : public IGameState {
public:
    explicit EditorState(IRenderAdapter& renderer);

    void onEnter() override;
    void onExit() override;
    void update(float dt) override;
    void render() override;

private:
    enum class EditorMode {
        Edit,
        Play
    };

    enum class GizmoOperation {
        Translate,
        Rotate,
        Scale
    };

    struct EntitySnapshot {
        Entity entity = kInvalidEntity;
        bool hasTag = false;
        Tag tag{};
        bool hasTransform = false;
        Transform transform{};
        bool hasMeshRenderer = false;
        MeshRenderer meshRenderer{};
        bool hasHierarchy = false;
        Hierarchy hierarchy{};
        bool hasSpin = false;
        Spin spin{};
        bool hasCamera = false;
        Camera camera{};
        bool hasRigidbody = false;
        Rigidbody rigidbody{};
        bool hasCollider = false;
        Collider collider{};
    };

    struct SceneSnapshot {
        std::vector<EntitySnapshot> entities;
        Entity selectedEntity = kInvalidEntity;
        Entity controllableEntity = kInvalidEntity;
        Entity gameCameraEntity = kInvalidEntity;
    };

    void bindActions();
    void createScene();
    bool createSceneFromManifest();
    void createFallbackScene();
    Entity createCubeEntity(const std::string& requestedName, const Vec3& position);
    Entity duplicateEntity(Entity source);
    void beginRename(Entity entity);
    void commitRename();
    Vec3 defaultSpawnPosition() const;
    void createGameCamera();
    void createEditorCameraEntity();
    void syncEditorCameraEntity();
    void setCameraMode();
    void updateGameplay(float dt, bool allowInput);
    void updateGameCamera(float dt, bool allowInput);
    void processGameplayInput(float dt);
    void renderDockSpace();
    void buildDefaultDockLayout(unsigned int dockspaceId);
    void renderMainMenu();
    void renderToolbar();
    void renderHierarchyPanel();
    void renderHierarchyEntity(Entity entity);
    void renderInspectorPanel();
    void renderStatisticsPanel();
    void renderViewportPanel();
    void renderViewportScene(int width, int height);
    void renderGizmo(const ImVec2& viewportMin, const ImVec2& viewportSize);
    void selectEntityAtViewportPosition(const ImVec2& viewportMin, const ImVec2& viewportSize);
    void startPlayMode();
    void stopPlayMode();
    SceneSnapshot captureSnapshot() const;
    void restoreSnapshot(const SceneSnapshot& snapshot);
    std::string entityLabel(Entity entity) const;
    bool isEditorEntity(Entity entity) const;
    Mat4 activeViewMatrix() const;
    Mat4 activeProjectionMatrix() const;

    IRenderAdapter& renderer_;
    World world_;
    EditorCamera editorCamera_;
    PhysicsSystem physicsSystem_;
    SpinSystem spinSystem_;
    RenderSystem renderSystem_;
    DebugRenderSystem debugRenderSystem_;
    Entity selectedEntity_ = kInvalidEntity;
    Entity controllableEntity_ = kInvalidEntity;
    Entity gameCameraEntity_ = kInvalidEntity;
    Entity editorCameraEntity_ = kInvalidEntity;
    Entity renamingEntity_ = kInvalidEntity;
    std::array<char, 128> renameBuffer_{};
    EditorMode mode_ = EditorMode::Edit;
    GizmoOperation gizmoOperation_ = GizmoOperation::Translate;
    bool gizmoLocalMode_ = true;
    bool showHierarchy_ = true;
    bool showInspector_ = true;
    bool showStatistics_ = true;
    bool showViewport_ = true;
    bool viewportHovered_ = false;
    bool viewportFocused_ = false;
    bool viewportInputActive_ = false;
    bool dockLayoutBuilt_ = false;
    bool lmbWasPressed_ = false;
    bool rmbWasPressed_ = false;
    bool mmbWasPressed_ = false;
    int viewportWidth_ = 1;
    int viewportHeight_ = 1;
    float lastDt_ = 0.0f;
    float fpsAverage_ = 0.0f;
    float fpsAccumulator_ = 0.0f;
    int fpsFrames_ = 0;
    SceneSnapshot playSnapshot_;
};
