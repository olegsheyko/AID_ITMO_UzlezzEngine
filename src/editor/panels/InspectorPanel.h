#pragma once

#include "ecs/Entity.h"
#include "editor/AssetDatabase.h"
#include "editor/TexturePicker.h"
#include "resources/Resource.h"
#include "resources/ResourceTypes.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class EditorContext;

class InspectorPanel {
public:
    void draw(EditorContext& context);

    bool open = true;

private:
    void drawEntity(EditorContext& context, Entity entity);
    void drawHeader(EditorContext& context, Entity entity);
    void drawTransform(EditorContext& context, Entity entity, bool locked);
    void drawMeshRenderer(EditorContext& context, Entity entity, bool locked);
    void drawAnimator(EditorContext& context, Entity entity);
    void drawRigidbody(EditorContext& context, Entity entity, bool locked);
    void drawCollider(EditorContext& context, Entity entity, bool locked);
    void drawSpin(EditorContext& context, Entity entity, bool locked);
    void drawCamera(EditorContext& context, Entity entity, bool locked);
    void drawAddComponent(EditorContext& context, Entity entity);
    void changeMesh(EditorContext& context, Entity entity, const std::string& meshPath);

    void drawAsset(EditorContext& context, const std::string& path);
    void drawAssetHeader(EditorContext& context, const AssetEntry& entry);
    void drawTextureAsset(const AssetEntry& entry);
    void drawModelAsset(EditorContext& context, const AssetEntry& entry);
    void drawMaterialAsset(EditorContext& context, const AssetEntry& entry);
    // Живой 3D-вид модели или материала: вращение мышью, зум колесом, анимация персонажа.
    void drawLivePreview(EditorContext& context, const std::string& path);
    void drawSceneAsset(EditorContext& context, const AssetEntry& entry);
    void drawTextAsset(const AssetEntry& entry);
    void drawAssetActions(EditorContext& context, const AssetEntry& entry);

    TexturePicker texturePicker_;
    Entity texturePickerTarget_ = 0;
    std::array<char, 64> componentSearch_{};
    std::array<char, 128> nameBuffer_{};
    Entity nameEntity_ = 0;

    std::string textPath_;
    std::filesystem::file_time_type textModified_{};
    std::string textContent_;
    std::vector<const char*> lineStarts_;
    std::vector<bool> lineInComment_;
    int textLines_ = 0;
    bool textTruncated_ = false;

    std::string previewPath_;
    float previewYaw_ = -0.62f;
    float previewPitch_ = -0.32f;
    float previewZoom_ = 1.0f;

    std::string modelPath_;
    std::shared_ptr<Resource<MeshData>> modelPreview_;

    std::string scenePath_;
    std::filesystem::file_time_type sceneModified_{};
    int sceneEntities_ = -1;
};
