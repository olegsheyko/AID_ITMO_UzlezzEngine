#pragma once

#include "ecs/Entity.h"
#include "editor/AssetDatabase.h"

#include <imgui.h>

class EditorContext;

// Типы перетаскиваемых данных ImGui между панелями.
constexpr const char* kEntityPayload = "UZ_ENTITY";
constexpr const char* kAssetPayload = "UZ_ASSET";

// Иконка и цвет сущности по её компонентам: камера, персонаж, меш, пустышка.
const char* entityIcon(const EditorContext& context, Entity entity, ImU32* outColor = nullptr);
const char* assetIcon(AssetType type, ImU32* outColor = nullptr);
ImU32 assetColor(AssetType type);
