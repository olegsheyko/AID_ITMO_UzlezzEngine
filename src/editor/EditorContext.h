#pragma once

#include "bench/LoadScenario.h"
#include "bench/StressRun.h"
#include "ecs/AnimationSystem.h"
#include "ecs/CollisionUtils.h"
#include "ecs/Components.h"
#include "ecs/DebugRenderSystem.h"
#include "ecs/Entity.h"
#include "ecs/PhysicsSystem.h"
#include "ecs/RenderSystem.h"
#include "ecs/SpinSystem.h"
#include "ecs/World.h"
#include "editor/AssetDatabase.h"
#include "editor/AssetPreview.h"
#include "editor/EditorCamera.h"
#include "scripting/ScriptComponent.h"
#include "scripting/ScriptSystem.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class IRenderAdapter;

enum class EditorMode {
    Edit,
    Play
};

enum class GizmoTool {
    Select,
    Translate,
    Rotate,
    Scale
};

// Цели рендера вьюпортов у IRenderAdapter.
constexpr int kSceneViewTarget = 0;
constexpr int kGameViewTarget = 1;

struct SceneViewSettings {
    GizmoTool tool = GizmoTool::Translate;
    bool localSpace = true;
    bool snap = false;
    float translateSnap = 0.25f;
    float rotateSnapDegrees = 15.0f;
    float scaleSnap = 0.1f;
    bool showGrid = true;
    float gridHeight = 0.0f;
    bool showCameraIcons = true;
    bool showSelectionOutline = true;
    bool showStats = false;
};

// Модель редактора: сцена, выделение, режим Play и команды над сущностями.
// Панели читают и меняют её напрямую — это общее состояние окна, а не чьё-то личное.
class EditorContext {
public:
    explicit EditorContext(IRenderAdapter& renderer);

    void enter();
    void exit();
    // Логика кадра без UI: статистика, симуляция в Play, анимация, лаб-инструменты.
    void update(float dt);

    // Сцена
    bool loadScene(const std::string& manifestPath);
    void reloadScene();
    const std::string& scenePath() const { return scenePath_; }
    std::string sceneName() const;

    // Сущности
    Entity createEmpty(const std::string& name, const Vec3& position);
    Entity createCube(const std::string& name, const Vec3& position);
    Entity createSphere(const std::string& name, const Vec3& position);
    Entity createModel(const std::string& path, const Vec3& position);
    Entity duplicate(Entity source);
    void destroy(Entity entity);
    // Перецепляет потомка, сохраняя его положение в мире. kInvalidEntity — в корень.
    bool setParent(Entity child, Entity parent);
    bool isAncestor(Entity ancestor, Entity entity) const;
    Entity parentOf(Entity entity) const;
    std::vector<Entity> childrenOf(Entity entity) const;
    std::vector<Entity> rootEntities() const;
    std::string displayName(Entity entity) const;
    bool isEditorEntity(Entity entity) const;
    bool isEditable(Entity entity) const;
    Vec3 defaultSpawnPosition() const;

    // Пространство мира
    Mat4 worldMatrix(Entity entity) const;
    Mat4 parentWorldMatrix(Entity entity) const;
    void setWorldMatrix(Entity entity, const Mat4& world);
    // Габариты в мире: меш (поза привязки), коллайдер или масштаб трансформа.
    bool worldBounds(Entity entity, AABB& outBounds) const;
    Entity pick(const Vec3& origin, const Vec3& direction, float* outDistance = nullptr, Entity ignore = kInvalidEntity) const;
    // Точка для бросания ассета: попадание в объект, иначе плоскость сетки, иначе перед камерой.
    Vec3 dropPoint(const Vec3& origin, const Vec3& direction, Entity ignore = kInvalidEntity) const;
    void focusSelection();

    // Переименование — общее для иерархии, меню и F2.
    void beginRename(Entity entity);
    void commitRename();
    void cancelRename();

    // Выделение: либо сущность, либо ассет, как Selection.activeObject в Unity.
    void select(Entity entity);
    void selectAsset(const std::string& path);
    void clearSelection();

    // Play
    void play();
    void stop();
    void setPaused(bool paused);
    void stepFrame();
    bool isPlaying() const { return mode == EditorMode::Play; }

    // Рендер вьюпортов в цели kSceneViewTarget / kGameViewTarget (размеры в пикселях).
    void renderSceneView(int width, int height);
    void renderGameView(int width, int height);
    void updateEditorCamera(float dt, int width, int height, bool inputEnabled, float mouseWheel);
    Mat4 gameViewMatrix() const;
    Mat4 gameProjectionMatrix(float aspect) const;

    // Перетаскивание модели из Content Browser во вьюпорт, как в UE5: модель появляется сразу
    // и едет за курсором; отпускание оставляет её в сцене, уход курсора — убирает.
    void updateDragPreview(const std::string& path, const Vec3& origin, const Vec3& direction);
    Entity commitDragPreview();
    void cancelDragPreview();
    // Материал или текстура, брошенные на объект: базовая текстура меша.
    bool applyAssetToEntity(const std::string& path, Entity entity);

    // Префабы и Lua (ЛР 2). Позиция из префаба — смещение от точки, куда его ставят.
    Entity spawnPrefab(const std::string& path, const Vec3& position);
    void loadArenaScene();
    bool isArenaScene() const;
    bool reloadScripts();
    bool savePrefabFields(Entity entity);

    // Лаб-инструменты (ЛР 1)
    void rebuildAnimationDemo();
    void removeAnimationDemo();

    IRenderAdapter& renderer;
    AssetDatabase assets{"assets"};
    AssetPreviewer previewer;
    World world;
    // Одна Lua VM на редактор; объявлена после world и уничтожается раньше него.
    ScriptSystem scripts{world};
    EditorCamera camera;
    PhysicsSystem physicsSystem;
    SpinSystem spinSystem;
    AnimationSystem animationSystem;
    RenderSystem renderSystem;
    DebugRenderSystem debugRenderSystem;

    Entity selected = kInvalidEntity;
    std::string selectedAsset;
    Entity controllableEntity = kInvalidEntity;
    Entity gameCameraEntity = kInvalidEntity;
    Entity editorCameraEntity = kInvalidEntity;
    Entity renamingEntity = kInvalidEntity;
    // Модель, которую сейчас тащат во вьюпорт, и объект под курсором при перетаскивании текстуры.
    Entity dragPreviewEntity = kInvalidEntity;
    Entity dropHighlight = kInvalidEntity;
    // Враг из префаба, показанный в Edit для настройки полей; в Play не участвует.
    Entity prefabPreview = kInvalidEntity;
    // Последнее сообщение скриптинга для панели Gameplay.
    std::string scriptMessage;
    bool scriptMessageIsError = false;
    std::array<char, 128> renameBuffer{};
    // Кадр, на котором начато переименование: поле ввода берёт фокус один раз.
    bool renameNeedsFocus = false;

    EditorMode mode = EditorMode::Edit;
    bool paused = false;
    SceneViewSettings sceneView;
    // Просьбы между панелями: показать ассет в Content Browser, вывести окно на передний план.
    std::string revealAssetRequest;
    std::string focusWindowRequest;
    // Флаги ввода, которые выставляют панели вьюпортов на прошлом кадре.
    bool sceneViewInputActive = false;
    bool gameViewInputActive = false;
    bool gameViewFocused = false;

    // Статистика кадра
    std::size_t sceneDrawnMeshes = 0;
    float lastDt = 0.0f;
    // Сглаженные за полсекунды значения — для подписей, чтобы цифры не мельтешили.
    float fpsAverage = 0.0f;
    float frameTimeAverageMs = 0.0f;
    static constexpr int kFrameHistory = 240;
    std::array<float, kFrameHistory> frameTimesMs{};
    int frameHistoryOffset = 0;

    // ЛР 1: тяжёлая загрузка, стресс, толпа с анимацией
    LoadScenario heavyLoad;
    bool heavyLoadAsync = true;
    StressRun stress;
    std::array<char, 1024> animationPath{"assets/models/animation/Walking.fbx"};
    std::string animationError;
    std::shared_ptr<Resource<MeshData>> animationLoad;
    std::vector<Entity> animationDemoEntities;
    int animationDemoCount = 16;
    bool animationYUp = true;

private:
    struct EntitySnapshot {
        Entity entity = kInvalidEntity;
        bool hasScript = false;
        ScriptComponent script{};
        bool hasTag = false;
        Tag tag{};
        bool hasTransform = false;
        Transform transform{};
        bool hasMeshRenderer = false;
        MeshRenderer meshRenderer{};
        bool hasAnimator = false;
        Animator animator{};
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
        Entity selected = kInvalidEntity;
        Entity prefabPreview = kInvalidEntity;
        Entity controllableEntity = kInvalidEntity;
        Entity gameCameraEntity = kInvalidEntity;
    };

    // Модель, брошенная в сцену: после загрузки подгоняем масштаб и ставим на опору.
    struct PendingModelFit {
        Entity entity = kInvalidEntity;
        Vec3 groundPoint{};
    };

    void bindActions();
    void resetSceneState();
    void setScriptMessage(const std::string& message, bool isError);
    void createScene();
    bool createSceneFromManifest();
    void createFallbackScene();
    void createGameCamera();
    void createEditorCameraEntity();
    void syncEditorCameraEntity(int width, int height);
    void setCameraMode();
    void activateCamera(Entity entity);
    void updateGameplay(float dt, bool allowInput);
    void updateGameCamera(float dt, bool allowInput);
    void processGameplayInput();
    void fitPendingModels();
    void placeGridOnGround();
    bool localMeshBounds(const MeshRenderer& meshRenderer, Vec3& outMin, Vec3& outMax) const;
    SceneSnapshot captureSnapshot() const;
    void restoreSnapshot(const SceneSnapshot& snapshot);

    std::string scenePath_;
    SceneSnapshot playSnapshot_;
    bool stepRequested_ = false;
    float fpsAccumulator_ = 0.0f;
    int fpsFrames_ = 0;
    bool lmbWasPressed_ = false;
    bool rmbWasPressed_ = false;
    bool mmbWasPressed_ = false;
    int sceneViewWidth_ = 1;
    int sceneViewHeight_ = 1;
    std::vector<PendingModelFit> pendingFits_;
    // Подгонка импортированной модели: поворот Y-up → Z-up, масштаб и смещение «низ по центру» к точке опоры.
    bool computeModelFit(const MeshRenderer& meshRenderer, Vec3& outRotation, float& outScale, Vec3& outOffset) const;
    std::string dragPreviewPath_;
    bool dragPreviewFitted_ = false;
    Vec3 dragPreviewOffset_{};
    Vec3 dragPreviewPoint_{};
    struct CachedBounds {
        Vec3 min{};
        Vec3 max{};
        std::size_t vertices = 0;
    };
    mutable std::unordered_map<const MeshData*, CachedBounds> boundsCache_;
};
