#include "editor/EditorIcons.h"

#include "editor/EditorContext.h"
#include "editor/EditorTheme.h"
#include "editor/IconsLucide.h"

const char* entityIcon(const EditorContext& context, Entity entity, ImU32* outColor) {
    const World& world = context.world;
    ImU32 color = IM_COL32(170, 186, 210, 255);
    const char* icon = ICON_LC_SQUARE_DASHED;
    if (!world.isAlive(entity)) {
        icon = ICON_LC_CIRCLE_HELP;
    } else if (world.hasComponent<Camera>(entity)) {
        icon = ICON_LC_VIDEO;
        color = IM_COL32(120, 176, 255, 255);
    } else if (world.hasComponent<Animator>(entity)) {
        icon = ICON_LC_PERSON_STANDING;
        color = IM_COL32(196, 146, 255, 255);
    } else if (entity == context.controllableEntity) {
        icon = ICON_LC_GAMEPAD_2;
        color = IM_COL32(120, 214, 160, 255);
    } else if (world.hasComponent<MeshRenderer>(entity)) {
        const std::string& mesh = world.getComponent<MeshRenderer>(entity).meshId;
        const bool primitive = mesh.rfind("primitive:", 0) == 0 || mesh == "cube" || mesh.find("cube") != std::string::npos;
        icon = primitive ? ICON_LC_BOX : ICON_LC_PACKAGE;
        color = IM_COL32(150, 196, 236, 255);
    } else if (!context.childrenOf(entity).empty()) {
        icon = ICON_LC_LAYERS;
        color = IM_COL32(200, 200, 206, 255);
    } else {
        color = IM_COL32(140, 140, 148, 255);
    }
    if (outColor) {
        *outColor = color;
    }
    return icon;
}

ImU32 assetColor(AssetType type) {
    using namespace EditorTheme;
    switch (type) {
    case AssetType::Folder: return kFolder;
    case AssetType::Texture: return IM_COL32(110, 190, 240, 255);
    case AssetType::Model: return kAssetModel;
    case AssetType::Material: return kAssetMaterial;
    case AssetType::Shader: return kAssetShader;
    case AssetType::Scene: return kAssetScene;
    case AssetType::Json: return IM_COL32(222, 196, 92, 255);
    case AssetType::Text: return kAssetText;
    case AssetType::Font: return kAssetFont;
    case AssetType::Audio: return kAssetAudio;
    case AssetType::Script: return IM_COL32(90, 140, 250, 255);
    case AssetType::Other: return kAssetGeneric;
    }
    return kAssetGeneric;
}

const char* assetIcon(AssetType type, ImU32* outColor) {
    if (outColor) {
        *outColor = assetColor(type);
    }
    switch (type) {
    case AssetType::Folder: return ICON_LC_FOLDER;
    case AssetType::Texture: return ICON_LC_FILE_IMAGE;
    case AssetType::Model: return ICON_LC_FILE_BOX;
    case AssetType::Material: return ICON_LC_PALETTE;
    case AssetType::Shader: return ICON_LC_FILE_CODE_2;
    case AssetType::Scene: return ICON_LC_CLAPPERBOARD;
    case AssetType::Json: return ICON_LC_FILE_JSON;
    case AssetType::Text: return ICON_LC_FILE_TEXT;
    case AssetType::Font: return ICON_LC_FILE_TYPE;
    case AssetType::Audio: return ICON_LC_FILE_AUDIO;
    case AssetType::Script: return ICON_LC_FILE_CODE;
    case AssetType::Other: return ICON_LC_FILE;
    }
    return ICON_LC_FILE;
}
