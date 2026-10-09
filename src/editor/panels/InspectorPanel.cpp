#include "editor/panels/InspectorPanel.h"

#include "editor/EditorContext.h"
#include "editor/EditorIcons.h"
#include "editor/EditorMath.h"
#include "editor/EditorTheme.h"
#include "editor/EditorWidgets.h"
#include "editor/EditorWindows.h"
#include "editor/MeshBounds.h"
#include "editor/IconsLucide.h"
#include "editor/PlatformShell.h"
#include "editor/SyntaxHighlight.h"
#include "editor/ThumbnailCache.h"
#include "resources/ResourceManager.h"
#include "resources/SceneManifest.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <type_traits>
#include <variant>

using namespace EditorTheme;

namespace {
constexpr std::size_t kMaxTextPreview = 256 * 1024;

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string fileName(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string shaderLabel(const std::string& key) {
    const std::size_t separator = key.find('|');
    if (separator == std::string::npos) {
        return fileName(key);
    }
    return fileName(key.substr(0, separator)) + "  |  " + fileName(key.substr(separator + 1));
}

// «pulse_radius» → «Pulse Radius», как Unity подписывает поля скриптов.
std::string nicifyFieldName(const std::string& name) {
    std::string result;
    bool upper = true;
    for (char c : name) {
        if (c == '_') {
            result += ' ';
            upper = true;
            continue;
        }
        result += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        upper = false;
    }
    return result;
}

void drawChecker(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float cell = 10.0f) {
    drawList->AddRectFilled(min, max, IM_COL32(50, 50, 54, 255), 6.0f);
    drawList->PushClipRect(min, max, true);
    for (float y = min.y; y < max.y; y += cell) {
        for (float x = min.x; x < max.x; x += cell) {
            if ((static_cast<int>((x - min.x) / cell) + static_cast<int>((y - min.y) / cell)) % 2 == 0) {
                drawList->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + cell, max.x), std::min(y + cell, max.y)), IM_COL32(62, 62, 66, 255));
            }
        }
    }
    drawList->PopClipRect();
}

void drawImageFit(ImDrawList* drawList, const TextureData& texture, const ImVec2& min, const ImVec2& max, float rounding) {
    const float width = max.x - min.x;
    const float height = max.y - min.y;
    const float scale = std::min(width / texture.width, height / texture.height);
    const ImVec2 size(texture.width * scale, texture.height * scale);
    const ImVec2 start(min.x + (width - size.x) * 0.5f, min.y + (height - size.y) * 0.5f);
    drawList->AddImageRounded(static_cast<ImTextureID>(texture.textureId), start, ImVec2(start.x + size.x, start.y + size.y),
        ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, rounding);
}

const TextureData* currentBaseTexture(const MeshRenderer& meshRenderer) {
    auto loaded = [](const std::shared_ptr<Resource<TextureData>>& resource) -> const TextureData* {
        return resource && resource->isLoaded() && resource->getData()->textureId != 0 ? resource->getData() : nullptr;
    };
    if (const TextureData* texture = loaded(meshRenderer.cachedBaseColorTexture)) {
        return texture;
    }
    if (meshRenderer.baseColorTextureId.empty() && meshRenderer.cachedMesh && meshRenderer.cachedMesh->isLoaded()) {
        for (const auto& subMesh : meshRenderer.cachedMesh->getData()->subMeshes) {
            if (const TextureData* texture = loaded(subMesh.material.cachedDiffuseTexture)) {
                return texture;
            }
        }
    }
    return nullptr;
}

// Строка «слот ассета»: миниатюра или иконка, имя, подпись и кнопки справа. true — просят открыть выбор.
// Принимает перетаскивание ассета типа acceptType из Content Browser: путь — в outDropped.
bool assetSlot(const char* id, const TextureData* thumbnail, const char* icon, ImU32 iconColor, const std::string& title,
    const std::string& subtitle, bool clearable, bool& outClear, AssetType acceptType, std::string& outDropped) {
    outClear = false;
    outDropped.clear();
    ImGui::PushID(id);
    const float height = std::max(40.0f, ImGui::GetFontSize() * 2.0f + EditorUI::px(12.0f));
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 max(min.x + width, min.y + height);
    ImGui::SetNextItemAllowOverlap();
    // Клик по полю открывает выбор, как Object Field в Unity.
    bool browse = ImGui::InvisibleButton("##slot", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    bool dropHover = false;
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* peek = ImGui::GetDragDropPayload();
        const bool matches = peek && peek->IsDataType(kAssetPayload) &&
            AssetDatabase::classify(static_cast<const char*>(peek->Data)) == acceptType;
        if (matches) {
            dropHover = true;
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                outDropped = static_cast<const char*>(payload->Data);
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(hovered ? kFrameHovered : kFrame), 5.0f);
    if (dropHover) {
        drawList->AddRect(min, max, ImGui::GetColorU32(kAccent), 5.0f, 0, 1.5f);
    }

    const ImVec2 thumbMin(min.x + 4.0f, min.y + 4.0f);
    const ImVec2 thumbMax(min.x + height - 4.0f, max.y - 4.0f);
    if (thumbnail) {
        drawChecker(drawList, thumbMin, thumbMax, 4.0f);
        drawImageFit(drawList, *thumbnail, thumbMin, thumbMax, 3.0f);
    } else {
        drawList->AddRectFilled(thumbMin, thumbMax, ImGui::GetColorU32(IM_COL32(36, 36, 39, 255)), 4.0f);
        EditorUI::drawTextCentered(drawList, ImVec2((thumbMin.x + thumbMax.x) * 0.5f, (thumbMin.y + thumbMax.y) * 0.5f), icon, iconColor);
    }

    const float buttonSize = ImGui::GetFrameHeight() - 2.0f;
    const float buttonsWidth = buttonSize * (clearable ? 2.0f : 1.0f) + 6.0f;
    const float textX = thumbMax.x + 8.0f;
    const float textWidth = max.x - buttonsWidth - textX - 4.0f;
    const float titleY = min.y + (height - ImGui::GetFontSize() * 1.95f) * 0.5f;
    EditorUI::drawTextEllipsis(drawList, ImVec2(textX, titleY), textWidth, title.c_str(), kText);
    const float subtitleY = titleY + ImGui::GetFontSize() + EditorUI::px(2.0f);
    EditorUI::pushSmallFont();
    EditorUI::drawTextEllipsis(drawList, ImVec2(textX, subtitleY), textWidth, subtitle.c_str(), kTextFaint);
    EditorUI::popFont();

    ImGui::SetCursorScreenPos(ImVec2(max.x - buttonsWidth, min.y + (height - buttonSize) * 0.5f));
    if (EditorUI::iconButton("##browse", ICON_LC_ELLIPSIS, "Browse...", false, buttonSize)) {
        browse = true;
    }
    if (clearable) {
        ImGui::SameLine(0.0f, 2.0f);
        if (EditorUI::iconButton("##clear", ICON_LC_X, "Clear", false, buttonSize)) {
            outClear = true;
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(min.x, max.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::PopID();
    return browse;
}

std::string readTextFile(const std::string& path, bool& truncated) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        truncated = false;
        return {};
    }
    std::string content;
    content.resize(kMaxTextPreview);
    file.read(content.data(), static_cast<std::streamsize>(content.size()));
    content.resize(static_cast<std::size_t>(file.gcount()));
    truncated = file.peek() != std::char_traits<char>::eof();
    // Нули и CR ломают отображение — убираем их.
    content.erase(std::remove(content.begin(), content.end(), '\r'), content.end());
    std::replace(content.begin(), content.end(), '\0', ' ');
    return content;
}
}

void InspectorPanel::draw(EditorContext& context) {
    if (!open) {
        return;
    }
    const bool visible = ImGui::Begin(EditorWindow::kInspector, &open);
    if (!visible) {
        ImGui::End();
        return;
    }

    if (context.isEditable(context.selected)) {
        drawEntity(context, context.selected);
    } else if (!context.selectedAsset.empty()) {
        drawAsset(context, context.selectedAsset);
    } else {
        EditorUI::emptyState(ICON_LC_MOUSE_POINTER_2, "Nothing selected", "Select an entity or an asset to inspect it");
    }

    std::string pickedTexture;
    if (texturePicker_.draw(pickedTexture) && context.isEditable(texturePickerTarget_) &&
        context.world.hasComponent<MeshRenderer>(texturePickerTarget_)) {
        MeshRenderer& meshRenderer = context.world.getComponent<MeshRenderer>(texturePickerTarget_);
        meshRenderer.baseColorTextureId = pickedTexture;
        meshRenderer.cachedBaseColorTexture = pickedTexture.empty()
            ? nullptr : ResourceManager::getInstance().loadTextureAsync(pickedTexture, JobPriority::High);
    }
    ImGui::End();
}

void InspectorPanel::drawEntity(EditorContext& context, Entity entity) {
    drawHeader(context, entity);
    const bool locked = context.isPlaying();
    if (locked) {
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = ImGui::GetFrameHeight() + 4.0f;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, ImVec2(min.x + width, min.y + height), ImGui::GetColorU32(kAccentSofter), 5.0f);
        drawList->AddRect(min, ImVec2(min.x + width, min.y + height), ImGui::GetColorU32(withAlpha(kAccent, 0.35f)), 5.0f);
        const float textY = min.y + (height - ImGui::GetFontSize()) * 0.5f;
        drawList->AddText(ImVec2(min.x + 10.0f, textY), ImGui::GetColorU32(kAccentHovered), ICON_LC_PLAY);
        EditorUI::drawTextEllipsis(drawList, ImVec2(min.x + 30.0f, textY), width - 40.0f, "Play mode: values are live and read-only", kText);
        ImGui::Dummy(ImVec2(width, height + 4.0f));
    }

    drawTransform(context, entity, locked);
    drawMeshRenderer(context, entity, locked);
    drawAnimator(context, entity);
    drawRigidbody(context, entity, locked);
    drawCollider(context, entity, locked);
    drawSpin(context, entity, locked);
    drawCamera(context, entity, locked);
    drawScript(context, entity, locked);

    ImGui::BeginDisabled(locked);
    drawAddComponent(context, entity);
    ImGui::EndDisabled();
}

void InspectorPanel::drawHeader(EditorContext& context, Entity entity) {
    ImU32 iconColor = 0;
    const char* icon = entityIcon(context, entity, &iconColor);
    const float tile = EditorUI::px(38.0f);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, ImVec2(min.x + tile, min.y + tile), ImGui::GetColorU32(kPanelRaised), 7.0f);
    ImGui::PushFont(nullptr, 20.0f);
    EditorUI::drawTextCentered(drawList, ImVec2(min.x + tile * 0.5f, min.y + tile * 0.5f), icon, iconColor);
    ImGui::PopFont();

    if (nameEntity_ != entity || !ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        if (nameEntity_ != entity || !ImGui::IsAnyItemActive()) {
            const std::string name = context.world.hasComponent<Tag>(entity) ? context.world.getComponent<Tag>(entity).name : std::string();
            std::snprintf(nameBuffer_.data(), nameBuffer_.size(), "%s", name.c_str());
            nameEntity_ = entity;
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(min.x + tile + 10.0f, min.y + 1.0f));
    const std::string idText = "ID " + std::to_string(entity);
    EditorUI::pushSmallFont();
    const float idWidth = ImGui::CalcTextSize(idText.c_str()).x + 12.0f;
    EditorUI::popFont();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - idWidth - 6.0f);
    ImGui::BeginDisabled(context.isPlaying());
    EditorUI::pushSemibold();
    if (ImGui::InputTextWithHint("##entity_name", "Name", nameBuffer_.data(), nameBuffer_.size())) {
        if (!context.world.hasComponent<Tag>(entity)) {
            context.world.addComponent<Tag>(entity);
        }
        context.world.getComponent<Tag>(entity).name = nameBuffer_.data();
    }
    EditorUI::popFont();
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 6.0f);
    {
        const ImVec2 pillMin = ImGui::GetCursorScreenPos();
        const float height = ImGui::GetFrameHeight();
        drawList->AddRectFilled(pillMin, ImVec2(pillMin.x + idWidth, pillMin.y + height), ImGui::GetColorU32(kPanelRaised), height * 0.5f);
        EditorUI::pushSmallFont();
        EditorUI::drawTextCentered(drawList, ImVec2(pillMin.x + idWidth * 0.5f, pillMin.y + height * 0.5f), idText.c_str(), kTextFaint);
        EditorUI::popFont();
        ImGui::Dummy(ImVec2(idWidth, height));
    }

    // Вторая строка: родитель и роль сущности.
    ImGui::SetCursorScreenPos(ImVec2(min.x + tile + 12.0f, min.y + ImGui::GetFrameHeight() + 3.0f));
    EditorUI::pushSmallFont();
    std::string subtitle;
    const Entity parent = context.parentOf(entity);
    if (entity == context.gameCameraEntity) {
        subtitle = "Main camera";
    } else if (entity == context.controllableEntity) {
        subtitle = "Player-controlled";
    } else if (context.world.hasComponent<Animator>(entity)) {
        subtitle = "Animated character";
    } else if (context.world.hasComponent<MeshRenderer>(entity)) {
        subtitle = "Mesh";
    } else {
        subtitle = "Empty";
    }
    if (parent != kInvalidEntity) {
        subtitle += "  \xC2\xB7  child of " + context.displayName(parent);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextFaint));
    ImGui::TextUnformatted(EditorUI::ellipsize(subtitle.c_str(), ImGui::GetContentRegionAvail().x).c_str());
    ImGui::PopStyleColor();
    EditorUI::popFont();
    ImGui::SetCursorScreenPos(ImVec2(min.x, std::max(ImGui::GetCursorScreenPos().y, min.y + tile) + 8.0f));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void InspectorPanel::drawTransform(EditorContext& context, Entity entity, bool locked) {
    if (!context.world.hasComponent<Transform>(entity)) {
        return;
    }
    Transform& transform = context.world.getComponent<Transform>(entity);
    EditorUI::ComponentAction action;
    if (EditorUI::componentHeader("transform", ICON_LC_MOVE_3D, "Transform", false, action)) {
        ImGui::BeginDisabled(locked);
        if (EditorUI::beginProperties("##transform")) {
            EditorUI::propertyVec3("Position", transform.position, 0.05f, 0.0f);
            Vec3 degrees{EditorMath::toDegrees(transform.rotation.x), EditorMath::toDegrees(transform.rotation.y), EditorMath::toDegrees(transform.rotation.z)};
            if (EditorUI::propertyVec3("Rotation", degrees, 0.5f, 0.0f, "%.1f\xC2\xB0")) {
                transform.rotation = {EditorMath::toRadians(degrees.x), EditorMath::toRadians(degrees.y), EditorMath::toRadians(degrees.z)};
            }
            if (EditorUI::propertyVec3("Scale", transform.scale, 0.02f, 1.0f)) {
                transform.scale.x = std::max(0.01f, transform.scale.x);
                transform.scale.y = std::max(0.01f, transform.scale.y);
                transform.scale.z = std::max(0.01f, transform.scale.z);
            }
            EditorUI::endProperties();
        }
        ImGui::EndDisabled();
        EditorUI::componentSpacing();
    }
    if (action == EditorUI::ComponentAction::Reset && !locked) {
        transform = Transform{};
    }
}

void InspectorPanel::changeMesh(EditorContext& context, Entity entity, const std::string& meshPath) {
    ResourceManager& resources = ResourceManager::getInstance();
    MeshRenderer& meshRenderer = context.world.getComponent<MeshRenderer>(entity);
    meshRenderer.meshId = meshPath;
    meshRenderer.yUpSource = MeshBounds::isImportedModel(meshPath);
    meshRenderer.cachedMesh = resources.loadMeshAsync(meshPath);
    meshRenderer.colliderBoundsInitialized = false;
    if (meshRenderer.cachedMesh && meshRenderer.cachedMesh->isLoaded()) {
        const auto& subMeshes = meshRenderer.cachedMesh->getData()->subMeshes;
        if (std::any_of(subMeshes.begin(), subMeshes.end(), [](const SubMesh& subMesh) { return !subMesh.material.diffuseTexturePath.empty(); })) {
            meshRenderer.baseColorTextureId.clear();
            meshRenderer.cachedBaseColorTexture.reset();
        }
    }
    if (context.world.hasComponent<Collider>(entity)) {
        // Коллайдер по умолчанию подгонится под новый меш, когда тот загрузится.
        Collider& collider = context.world.getComponent<Collider>(entity);
        if (collider.type == ColliderType::Box) {
            collider.halfExtents = {0.5f, 0.5f, 0.5f};
            collider.offset = {};
        }
    }
}

void InspectorPanel::drawMeshRenderer(EditorContext& context, Entity entity, bool locked) {
    if (!context.world.hasComponent<MeshRenderer>(entity)) {
        return;
    }
    MeshRenderer& meshRenderer = context.world.getComponent<MeshRenderer>(entity);
    ResourceManager& resources = ResourceManager::getInstance();
    EditorUI::ComponentAction action;
    if (EditorUI::componentHeader("mesh_renderer", ICON_LC_BOX, "Mesh Renderer", true, action)) {
        ImGui::BeginDisabled(locked);
        if (EditorUI::beginProperties("##mesh_renderer")) {
            EditorUI::propertyCheckbox("Visible", meshRenderer.visible);
            EditorUI::propertyLabel("Y-Up Source", "The mesh was authored Y-up (FBX, glTF): rotate it into the Z-up world.\n"
                "Turn off for models exported Z-up.");
            if (EditorUI::checkbox("##y_up", &meshRenderer.yUpSource)) {
                meshRenderer.colliderBoundsInitialized = false;
            }

            // Меш
            EditorUI::propertyLabel("Mesh");
            {
                std::string subtitle = "Not loaded";
                if (meshRenderer.cachedMesh) {
                    if (meshRenderer.cachedMesh->isPending()) {
                        subtitle = "Loading...";
                    } else if (meshRenderer.cachedMesh->isFailed()) {
                        subtitle = "Failed to load";
                    } else if (const MeshData* data = meshRenderer.cachedMesh->getData()) {
                        // Старые простые меши хранят геометрию в самом MeshData, остальные — в подмешах.
                        std::size_t triangles = data->subMeshes.empty() ? data->indexCount / 3 : 0;
                        for (const SubMesh& subMesh : data->subMeshes) {
                            triangles += subMesh.indexCount / 3;
                        }
                        subtitle = std::to_string(triangles) + " triangles";
                        if (data->subMeshes.size() > 1) {
                            subtitle += "  \xC2\xB7  " + std::to_string(data->subMeshes.size()) + " submeshes";
                        }
                    }
                }
                bool clear = false;
                std::string dropped;
                const std::string title = meshRenderer.meshId.empty() ? std::string("None") : fileName(meshRenderer.meshId);
                if (assetSlot("mesh_slot", nullptr, ICON_LC_BOX, assetColor(AssetType::Model), title, subtitle, false, clear,
                        AssetType::Model, dropped)) {
                    ImGui::OpenPopup("##mesh_picker");
                }
                if (!dropped.empty()) {
                    changeMesh(context, entity, dropped);
                }
                ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetContentRegionAvail().x, 0.0f), ImVec2(FLT_MAX, 420.0f));
                if (ImGui::BeginPopup("##mesh_picker")) {
                    EditorUI::textFaint("Loaded meshes");
                    for (const std::string& id : resources.getMeshIds()) {
                        if (ImGui::Selectable((std::string(ICON_LC_BOX "  ") + fileName(id) + "##" + id).c_str(), id == meshRenderer.meshId)) {
                            changeMesh(context, entity, id);
                        }
                    }
                    ImGui::Separator();
                    EditorUI::textFaint("Project models");
                    for (const AssetEntry& model : context.assets.search(".", 2000)) {
                        if (model.type != AssetType::Model) {
                            continue;
                        }
                        if (ImGui::Selectable((std::string(ICON_LC_FILE_BOX "  ") + model.name + "##" + model.path).c_str(), model.path == meshRenderer.meshId)) {
                            changeMesh(context, entity, model.path);
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                            ImGui::SetTooltip("%s", model.path.c_str());
                        }
                    }
                    ImGui::EndPopup();
                }
            }

            // Базовая текстура
            EditorUI::propertyLabel("Base Texture", "Overrides the diffuse texture of the mesh materials");
            {
                const TextureData* texture = currentBaseTexture(meshRenderer);
                std::string title = meshRenderer.baseColorTextureId.empty() ? std::string("Material default") : fileName(meshRenderer.baseColorTextureId);
                std::string subtitle;
                if (texture) {
                    subtitle = std::to_string(texture->width) + " \xC3\x97 " + std::to_string(texture->height);
                } else if (meshRenderer.cachedBaseColorTexture && meshRenderer.cachedBaseColorTexture->isPending()) {
                    subtitle = "Loading...";
                } else {
                    subtitle = meshRenderer.baseColorTextureId.empty() ? "Uses material color" : "Missing";
                }
                bool clear = false;
                std::string dropped;
                if (assetSlot("texture_slot", texture, ICON_LC_IMAGE, assetColor(AssetType::Texture), title, subtitle,
                        !meshRenderer.baseColorTextureId.empty(), clear, AssetType::Texture, dropped)) {
                    texturePickerTarget_ = entity;
                    texturePicker_.open(meshRenderer.baseColorTextureId);
                }
                if (clear) {
                    meshRenderer.baseColorTextureId.clear();
                    meshRenderer.cachedBaseColorTexture.reset();
                }
                if (!dropped.empty()) {
                    meshRenderer.baseColorTextureId = dropped;
                    meshRenderer.cachedBaseColorTexture = resources.loadTextureAsync(dropped, JobPriority::High);
                }
            }

            // Шейдер
            EditorUI::propertyLabel("Shader");
            const std::string current = meshRenderer.shaderId.empty() ? std::string("None") : shaderLabel(meshRenderer.shaderId);
            if (EditorUI::beginCombo("##shader", current.c_str())) {
                for (const std::string& id : resources.getShaderIds()) {
                    const bool selected = meshRenderer.shaderId == id;
                    if (ImGui::Selectable((shaderLabel(id) + "##" + id).c_str(), selected)) {
                        const std::size_t separator = id.find('|');
                        if (separator != std::string::npos) {
                            meshRenderer.shaderId = id;
                            meshRenderer.cachedShader = resources.loadShader(id.substr(0, separator), id.substr(separator + 1));
                        }
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (meshRenderer.cachedShader && meshRenderer.cachedShader->isFailed()) {
                EditorUI::propertyLabel("");
                ImGui::TextColored(toVec4(kError), ICON_LC_CIRCLE_ALERT "  Shader failed to compile");
            }
            EditorUI::endProperties();
        }
        ImGui::EndDisabled();
        EditorUI::componentSpacing();
    }
    if (!locked) {
        if (action == EditorUI::ComponentAction::Reset) {
            meshRenderer.visible = true;
            meshRenderer.baseColorTextureId.clear();
            meshRenderer.cachedBaseColorTexture.reset();
        } else if (action == EditorUI::ComponentAction::Remove) {
            context.world.removeComponent<Animator>(entity);
            context.world.removeComponent<MeshRenderer>(entity);
        }
    }
}

void InspectorPanel::drawAnimator(EditorContext& context, Entity entity) {
    if (!context.world.hasComponent<Animator>(entity) || !context.world.hasComponent<MeshRenderer>(entity)) {
        return;
    }
    Animator& animator = context.world.getComponent<Animator>(entity);
    const auto& mesh = context.world.getComponent<MeshRenderer>(entity).cachedMesh;
    EditorUI::ComponentAction action;
    if (EditorUI::componentHeader("animator", ICON_LC_PERSON_STANDING, "Animator", !context.isPlaying(), action)) {
        if (mesh && mesh->isLoaded() && !mesh->getData()->skeleton.clips.empty()) {
            const auto& clips = mesh->getData()->skeleton.clips;
            if (animator.clip >= clips.size()) {
                animator.clip = 0;
            }
            if (EditorUI::beginProperties("##animator")) {
                EditorUI::propertyLabel("Clip");
                if (EditorUI::beginCombo("##clip", clips[animator.clip].name.c_str())) {
                    for (unsigned int i = 0; i < clips.size(); ++i) {
                        ImGui::PushID(static_cast<int>(i));
                        if (ImGui::Selectable(clips[i].name.c_str(), animator.clip == i)) {
                            animator.clip = i;
                            animator.time = 0;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                EditorUI::propertyLabel("Playback");
                if (EditorUI::iconButton("##toggle", animator.paused ? ICON_LC_PLAY : ICON_LC_PAUSE, animator.paused ? "Play" : "Pause")) {
                    animator.paused = !animator.paused;
                }
                ImGui::SameLine(0.0f, 6.0f);
                float time = static_cast<float>(animator.time);
                const float duration = static_cast<float>(clips[animator.clip].duration);
                ImGui::SetNextItemWidth(-FLT_MIN);
                char overlay[32];
                std::snprintf(overlay, sizeof(overlay), "%.2f / %.2f s", time, duration);
                if (ImGui::SliderFloat("##time", &time, 0.0f, duration, overlay)) {
                    animator.time = time;
                }
                EditorUI::propertyLabel("Speed");
                ImGui::SliderFloat("##speed", &animator.speed, -2.0f, 3.0f, "%.2fx");
                EditorUI::endProperties();
            }
            EditorUI::pushSmallFont();
            EditorUI::textFaint("Global pause and speed are in Renderer Info \xE2\x86\x92 Animation.");
            EditorUI::popFont();
        } else {
            EditorUI::textFaint(mesh && mesh->isPending() ? "Loading animation clips..." : "Bind pose: the mesh has no animation clips.");
        }
        EditorUI::componentSpacing();
    }
    if (action == EditorUI::ComponentAction::Reset) {
        animator.time = 0;
        animator.speed = 1.0f;
        animator.paused = false;
    } else if (action == EditorUI::ComponentAction::Remove) {
        context.world.removeComponent<Animator>(entity);
    }
}

void InspectorPanel::drawRigidbody(EditorContext& context, Entity entity, bool locked) {
    if (!context.world.hasComponent<Rigidbody>(entity)) {
        return;
    }
    Rigidbody& rigidbody = context.world.getComponent<Rigidbody>(entity);
    EditorUI::ComponentAction action;
    if (EditorUI::componentHeader("rigidbody", ICON_LC_WEIGHT, "Rigidbody", true, action)) {
        if (EditorUI::beginProperties("##rigidbody")) {
            ImGui::BeginDisabled(locked);
            EditorUI::propertyFloat("Mass", rigidbody.mass, 0.05f, 0.0f, 1000.0f, "%.2f kg", 1.0f);
            EditorUI::propertyCheckbox("Use Gravity", rigidbody.useGravity);
            ImGui::EndDisabled();
            ImGui::BeginDisabled(true);
            Vec3 velocity = rigidbody.velocity;
            Vec3 acceleration = rigidbody.acceleration;
            EditorUI::propertyVec3("Velocity", velocity, 0.0f, 0.0f, "%.2f");
            EditorUI::propertyVec3("Acceleration", acceleration, 0.0f, 0.0f, "%.2f");
            ImGui::EndDisabled();
            EditorUI::endProperties();
        }
        EditorUI::componentSpacing();
    }
    if (!locked) {
        if (action == EditorUI::ComponentAction::Reset) {
            rigidbody = Rigidbody{};
        } else if (action == EditorUI::ComponentAction::Remove) {
            context.world.removeComponent<Rigidbody>(entity);
        }
    }
}

void InspectorPanel::drawCollider(EditorContext& context, Entity entity, bool locked) {
    if (!context.world.hasComponent<Collider>(entity)) {
        return;
    }
    Collider& collider = context.world.getComponent<Collider>(entity);
    EditorUI::ComponentAction action;
    const char* title = collider.type == ColliderType::Box ? "Box Collider" : "Sphere Collider";
    const char* icon = collider.type == ColliderType::Box ? ICON_LC_SQUARE_DASHED : ICON_LC_CIRCLE_DASHED;
    if (EditorUI::componentHeader("collider", icon, title, true, action, ICON_LC_SCAN "  Fit to Mesh")) {
        ImGui::BeginDisabled(locked);
        if (EditorUI::beginProperties("##collider")) {
            EditorUI::propertyLabel("Shape");
            int typeIndex = collider.type == ColliderType::Sphere ? 1 : 0;
            const char* types[] = {"Box", "Sphere"};
            if (EditorUI::combo("##shape", &typeIndex, types, 2)) {
                collider.type = typeIndex == 1 ? ColliderType::Sphere : ColliderType::Box;
            }
            EditorUI::propertyVec3("Offset", collider.offset, 0.03f, 0.0f);
            if (collider.type == ColliderType::Box) {
                if (EditorUI::propertyVec3("Half Extents", collider.halfExtents, 0.03f, 0.5f)) {
                    collider.halfExtents.x = std::max(0.01f, collider.halfExtents.x);
                    collider.halfExtents.y = std::max(0.01f, collider.halfExtents.y);
                    collider.halfExtents.z = std::max(0.01f, collider.halfExtents.z);
                }
            } else {
                EditorUI::propertyFloat("Radius", collider.radius, 0.03f, 0.01f, 100.0f, "%.3f", 0.5f);
            }
            EditorUI::endProperties();
        }
        ImGui::EndDisabled();
        EditorUI::componentSpacing();
    }
    if (!locked) {
        if (action == EditorUI::ComponentAction::Reset) {
            collider = Collider{};
        } else if (action == EditorUI::ComponentAction::Extra) {
            collider = Collider{};
            if (context.world.hasComponent<MeshRenderer>(entity)) {
                context.world.getComponent<MeshRenderer>(entity).colliderBoundsInitialized = false;
            }
        } else if (action == EditorUI::ComponentAction::Remove) {
            context.world.removeComponent<Collider>(entity);
        }
    }
}

void InspectorPanel::drawSpin(EditorContext& context, Entity entity, bool locked) {
    if (!context.world.hasComponent<Spin>(entity)) {
        return;
    }
    Spin& spin = context.world.getComponent<Spin>(entity);
    EditorUI::ComponentAction action;
    if (EditorUI::componentHeader("spin", ICON_LC_ROTATE_CW, "Spin", true, action)) {
        ImGui::BeginDisabled(locked);
        if (EditorUI::beginProperties("##spin")) {
            EditorUI::propertyFloat("Speed", spin.speed, 0.02f, 0.0f, 0.0f, "%.2f rad/s", 0.0f);
            EditorUI::endProperties();
        }
        ImGui::EndDisabled();
        EditorUI::componentSpacing();
    }
    if (!locked) {
        if (action == EditorUI::ComponentAction::Reset) {
            spin = Spin{};
        } else if (action == EditorUI::ComponentAction::Remove) {
            context.world.removeComponent<Spin>(entity);
        }
    }
}

void InspectorPanel::drawCamera(EditorContext& context, Entity entity, bool locked) {
    if (!context.world.hasComponent<Camera>(entity)) {
        return;
    }
    Camera& camera = context.world.getComponent<Camera>(entity);
    EditorUI::ComponentAction action;
    const bool isMain = entity == context.gameCameraEntity;
    if (EditorUI::componentHeader("camera", ICON_LC_VIDEO, "Camera", true, action, isMain ? nullptr : ICON_LC_GAMEPAD_2 "  Set as Main Camera")) {
        ImGui::BeginDisabled(locked);
        if (EditorUI::beginProperties("##camera")) {
            EditorUI::propertyLabel("Field of View");
            ImGui::SliderFloat("##fov", &camera.fovDegrees, 10.0f, 120.0f, "%.0f\xC2\xB0");
            EditorUI::propertyFloat("Near Clip", camera.nearClip, 0.01f, 0.01f, 10.0f, "%.2f", 0.1f);
            EditorUI::propertyFloat("Far Clip", camera.farClip, 1.0f, 1.0f, 5000.0f, "%.0f", 100.0f);
            EditorUI::propertyLabel("Role");
            ImGui::AlignTextToFramePadding();
            if (isMain) {
                ImGui::TextColored(toVec4(kAccentHovered), ICON_LC_GAMEPAD_2 "  Main camera (Game view)");
            } else {
                EditorUI::textFaint("Secondary camera");
            }
            EditorUI::endProperties();
        }
        ImGui::EndDisabled();
        EditorUI::componentSpacing();
    }
    if (!locked) {
        if (action == EditorUI::ComponentAction::Reset) {
            camera.fovDegrees = 45.0f;
            camera.nearClip = 0.1f;
            camera.farClip = 100.0f;
        } else if (action == EditorUI::ComponentAction::Extra) {
            context.gameCameraEntity = entity;
        } else if (action == EditorUI::ComponentAction::Remove) {
            context.world.removeComponent<Camera>(entity);
        }
    }
}

void InspectorPanel::drawScript(EditorContext& context, Entity entity, bool locked) {
    if (!context.world.hasComponent<ScriptComponent>(entity)) {
        return;
    }
    ScriptComponent& script = context.world.getComponent<ScriptComponent>(entity);
    const std::string title = script.className.empty() ? std::string("Script") : script.className + " (Script)";
    const bool canSave = !locked && !script.prefab.empty();
    EditorUI::ComponentAction action;
    if (EditorUI::componentHeader("script", ICON_LC_FILE_CODE, title.c_str(), !locked, action,
            canSave ? ICON_LC_SAVE "  Save Fields to Prefab" : nullptr)) {
        if (EditorUI::beginProperties("##script")) {
            // Файлы скрипта и префаба — ссылки: клик показывает их в Content Browser.
            auto assetLink = [&context](const char* label, const char* icon, const std::string& path) {
                ImGui::PushID(label);
                EditorUI::propertyLabel(label);
                const std::string text = std::string(icon) + "  " + fileName(path);
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
                if (ImGui::Button(text.c_str(), ImVec2(-FLT_MIN, 0.0f))) {
                    context.revealAssetRequest = path;
                }
                ImGui::PopStyleVar();
                ImGui::SetItemTooltip("%s\nClick to show in the Content Browser", path.c_str());
                ImGui::PopID();
            };
            assetLink("Script", ICON_LC_FILE_CODE, script.path);
            if (!script.prefab.empty()) {
                assetLink("Prefab", ICON_LC_PACKAGE, script.prefab);
            }
            ImGui::BeginDisabled(locked);
            for (auto& [name, value] : script.fields) {
                const std::string label = nicifyFieldName(name);
                ImGui::PushID(name.c_str());
                std::visit([&label](auto& field) {
                    using T = std::decay_t<decltype(field)>;
                    if constexpr (std::is_same_v<T, bool>) {
                        EditorUI::propertyCheckbox(label.c_str(), field);
                    } else if constexpr (std::is_same_v<T, int>) {
                        EditorUI::propertyLabel(label.c_str(), "Integer field");
                        ImGui::DragInt("##value", &field, 0.1f);
                    } else if constexpr (std::is_same_v<T, double>) {
                        EditorUI::propertyLabel(label.c_str(), "Number field");
                        ImGui::DragScalar("##value", ImGuiDataType_Double, &field, 0.02f, nullptr, nullptr, "%.3f");
                    } else {
                        char buffer[512] = {};
                        std::copy_n(field.data(), std::min(field.size(), sizeof(buffer) - 1), buffer);
                        EditorUI::propertyLabel(label.c_str(), "String field");
                        if (ImGui::InputText("##value", buffer, sizeof(buffer))) {
                            field = buffer;
                        }
                    }
                }, value);
                ImGui::PopID();
            }
            ImGui::EndDisabled();
            EditorUI::endProperties();
        }
        if (script.fields.empty()) {
            EditorUI::textFaint("No fields: declare them in the Lua class.");
        }
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        if (ImGui::Button(ICON_LC_FILE_PEN "  Edit Script", ImVec2(-FLT_MIN, 0.0f))) {
            context.openScriptRequest = script.path;
        }
        ImGui::SetItemTooltip("Open %s in the Script Editor. Works in Play too: saved changes reload in the running game.", script.path.c_str());
        if (!locked) {
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float half = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
            ImGui::BeginDisabled(!canSave);
            if (ImGui::Button(ICON_LC_SAVE "  Save to Prefab", ImVec2(half, 0.0f))) {
                action = EditorUI::ComponentAction::Extra;
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("Write these values into %s.\nNew spawns and scene loads use the prefab, not this copy.",
                script.prefab.empty() ? "the source prefab" : script.prefab.c_str());
            ImGui::SameLine();
            if (ImGui::Button(ICON_LC_REFRESH_CW "  Reload Scripts", ImVec2(-FLT_MIN, 0.0f))) {
                const bool ok = context.reloadScripts();
                scriptResultEntity_ = entity;
                scriptResult_ = ok ? "Scripts reloaded." : context.scripts.error();
                scriptResultError_ = !ok;
            }
            if (scriptResultEntity_ == entity && !scriptResult_.empty()) {
                EditorUI::pushSmallFont();
                ImGui::PushStyleColor(ImGuiCol_Text, toVec4(scriptResultError_ ? kError : kSuccess));
                ImGui::TextWrapped("%s  %s", scriptResultError_ ? ICON_LC_CIRCLE_ALERT : ICON_LC_CHECK, scriptResult_.c_str());
                ImGui::PopStyleColor();
                EditorUI::popFont();
            }
        }
        EditorUI::componentSpacing();
    }
    if (action == EditorUI::ComponentAction::Extra && canSave) {
        const bool ok = context.savePrefabFields(entity);
        scriptResultEntity_ = entity;
        scriptResult_ = ok ? "Saved to " + fileName(script.prefab) + "." : context.scriptMessage;
        scriptResultError_ = !ok;
    } else if (action == EditorUI::ComponentAction::Reset && !locked) {
        // Сброс к значениям по умолчанию из Lua-класса. В Play поля живые — не трогаем.
        script.fields.clear();
        context.scripts.attachDefaults(entity);
    } else if (action == EditorUI::ComponentAction::Remove) {
        context.world.removeComponent<ScriptComponent>(entity);
    }
}

void InspectorPanel::drawAddComponent(EditorContext& context, Entity entity) {
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    const float width = std::min(240.0f, ImGui::GetContentRegionAvail().x);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - width) * 0.5f);
    if (ImGui::Button(ICON_LC_PLUS "  Add Component", ImVec2(width, ImGui::GetFrameHeight() + 4.0f))) {
        componentSearch_.fill('\0');
        ImGui::OpenPopup("##add_component");
    }
    const ImVec2 buttonMin = ImGui::GetItemRectMin();
    ImGui::SetNextWindowPos(ImVec2(buttonMin.x, ImGui::GetItemRectMax().y + 4.0f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
    if (!ImGui::BeginPopup("##add_component")) {
        return;
    }
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    EditorUI::searchBox("component_search", componentSearch_.data(), componentSearch_.size(), "Search components");
    ImGui::Separator();

    World& world = context.world;
    struct Option {
        const char* icon;
        const char* name;
        bool available;
        int kind;
    };
    bool hasClips = false;
    if (world.hasComponent<MeshRenderer>(entity)) {
        const auto& mesh = world.getComponent<MeshRenderer>(entity).cachedMesh;
        hasClips = mesh && mesh->isLoaded() && !mesh->getData()->skeleton.clips.empty();
    }
    const Option options[] = {
        {ICON_LC_BOX, "Mesh Renderer", !world.hasComponent<MeshRenderer>(entity), 0},
        {ICON_LC_WEIGHT, "Rigidbody", !world.hasComponent<Rigidbody>(entity), 1},
        {ICON_LC_SQUARE_DASHED, "Box Collider", !world.hasComponent<Collider>(entity), 2},
        {ICON_LC_CIRCLE_DASHED, "Sphere Collider", !world.hasComponent<Collider>(entity), 3},
        {ICON_LC_ROTATE_CW, "Spin", !world.hasComponent<Spin>(entity), 4},
        {ICON_LC_VIDEO, "Camera", !world.hasComponent<Camera>(entity), 5},
        {ICON_LC_PERSON_STANDING, "Animator", !world.hasComponent<Animator>(entity) && hasClips, 6},
    };
    const std::string needle = lower(componentSearch_.data());
    int shown = 0;
    int firstAvailable = -1;
    for (const Option& option : options) {
        if (!needle.empty() && lower(option.name).find(needle) == std::string::npos) {
            continue;
        }
        ++shown;
        if (option.available && firstAvailable < 0) {
            firstAvailable = option.kind;
        }
        ImGui::BeginDisabled(!option.available);
        const std::string label = std::string(option.icon) + "  " + option.name;
        bool chosen = ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_None, ImVec2(0.0f, ImGui::GetFrameHeight()));
        ImGui::EndDisabled();
        if (!chosen && option.available && option.kind == firstAvailable && !needle.empty() && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            chosen = true;
        }
        if (!chosen) {
            continue;
        }
        switch (option.kind) {
        case 0: {
            MeshRenderer meshRenderer;
            meshRenderer.meshId = "primitive:cube";
            meshRenderer.cachedMesh = ResourceManager::getInstance().loadMeshAsync(meshRenderer.meshId);
            meshRenderer.shaderId = ResourceManager::makeShaderKey("assets/shaders/mesh_vertex.glsl", "assets/shaders/mesh_fragment_simple_texture.glsl");
            meshRenderer.cachedShader = ResourceManager::getInstance().loadShader("assets/shaders/mesh_vertex.glsl", "assets/shaders/mesh_fragment_simple_texture.glsl");
            world.addComponent<MeshRenderer>(entity, meshRenderer);
            break;
        }
        case 1: world.addComponent<Rigidbody>(entity); break;
        case 2: world.addComponent<Collider>(entity); break;
        case 3: world.addComponent<Collider>(entity, Collider{ColliderType::Sphere, Vec3{0.5f, 0.5f, 0.5f}, Vec3{}, 0.5f}); break;
        case 4: world.addComponent<Spin>(entity, Spin{1.0f}); break;
        case 5: world.addComponent<Camera>(entity, Camera{45.0f, 0.1f, 100.0f, 16.0f / 9.0f, false, Mat4::identity(), Mat4::identity()}); break;
        case 6: world.addComponent<Animator>(entity); break;
        default: break;
        }
        if (world.hasComponent<MeshRenderer>(entity) && option.kind == 2) {
            world.getComponent<MeshRenderer>(entity).colliderBoundsInitialized = false;
        }
        ImGui::CloseCurrentPopup();
    }
    if (shown == 0) {
        EditorUI::textFaint("No components match");
    }
    ImGui::EndPopup();
}

void InspectorPanel::drawAsset(EditorContext& context, const std::string& path) {
    const AssetEntry* entry = context.assets.find(path);
    if (entry == nullptr) {
        EditorUI::emptyState(ICON_LC_FILE_QUESTION, "Asset not found", path.c_str());
        return;
    }
    drawAssetHeader(context, *entry);
    switch (entry->type) {
    case AssetType::Texture:
        drawTextureAsset(*entry);
        break;
    case AssetType::Model:
        drawModelAsset(context, *entry);
        break;
    case AssetType::Scene:
        drawSceneAsset(context, *entry);
        break;
    case AssetType::Material:
        drawMaterialAsset(context, *entry);
        drawAssetActions(context, *entry);
        drawTextAsset(*entry);
        return;
    case AssetType::Prefab:
        ImGui::BeginDisabled(context.isPlaying());
        if (EditorUI::primaryButton(ICON_LC_PACKAGE "  Add to Scene", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() + 4.0f))) {
            context.select(context.spawnPrefab(entry->path, context.dropPoint(context.camera.getPosition(), context.camera.getForward())));
        }
        ImGui::EndDisabled();
        drawAssetActions(context, *entry);
        drawTextAsset(*entry);
        return;
    case AssetType::Script:
        if (entry->extension == ".lua") {
            // Правка прямо в редакторе; внешний редактор остаётся кнопкой «Open Externally» ниже.
            if (EditorUI::primaryButton(ICON_LC_FILE_PEN "  Edit Script", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() + 4.0f))) {
                context.openScriptRequest = entry->path;
            }
            ImGui::SetItemTooltip("Edit this script in the Script Editor. Saved scripts reload in the running game.");
        }
        drawAssetActions(context, *entry);
        drawTextAsset(*entry);
        return;
    case AssetType::Shader:
    case AssetType::Json:
    case AssetType::Text:
        drawAssetActions(context, *entry);
        drawTextAsset(*entry);
        return;
    case AssetType::Folder:
        break;
    default:
        break;
    }
    drawAssetActions(context, *entry);
}

void InspectorPanel::drawAssetHeader(EditorContext& context, const AssetEntry& entry) {
    const float tile = 52.0f;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const bool previewable = entry.type == AssetType::Texture && AssetDatabase::isLoadableTexture(entry.extension);
    const TextureData* thumbnail = previewable ? ThumbnailCache::instance().texture(entry.path) : nullptr;
    const AssetThumbnail model = AssetPreviewer::supports(entry.type) ? context.previewer.thumbnail(entry.path) : AssetThumbnail{};
    if (thumbnail) {
        drawChecker(drawList, min, ImVec2(min.x + tile, min.y + tile), 6.0f);
        drawImageFit(drawList, *thumbnail, min, ImVec2(min.x + tile, min.y + tile), 4.0f);
    } else if (model.texture != 0) {
        drawList->AddImageRounded(static_cast<ImTextureID>(model.texture), min, ImVec2(min.x + tile, min.y + tile),
            ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, 8.0f);
    } else {
        ImU32 color = 0;
        const char* icon = assetIcon(entry.type, &color);
        drawList->AddRectFilled(min, ImVec2(min.x + tile, min.y + tile), ImGui::GetColorU32(kPanelRaised), 8.0f);
        ImGui::PushFont(nullptr, 26.0f);
        EditorUI::drawTextCentered(drawList, ImVec2(min.x + tile * 0.5f, min.y + tile * 0.5f), icon, color);
        ImGui::PopFont();
    }
    const float textX = min.x + tile + 12.0f;
    const float textWidth = ImGui::GetContentRegionAvail().x - tile - 12.0f;
    ImGui::PushFont(fonts().semibold, 15.0f);
    EditorUI::drawTextEllipsis(drawList, ImVec2(textX, min.y + 1.0f), textWidth, entry.name.c_str(), kText);
    ImGui::PopFont();
    const std::string badgeText = entry.type == AssetType::Folder ? std::string("FOLDER") : AssetDatabase::badgeText(entry);
    EditorUI::badge(drawList, ImVec2(textX, min.y + 22.0f), badgeText.c_str(), withAlpha(assetColor(entry.type), 0.22f), assetColor(entry.type));
    const float badgeWidth = EditorUI::badgeSize(badgeText.c_str()).x;
    EditorUI::pushSmallFont();
    std::string info = AssetDatabase::typeName(entry.type);
    if (entry.type != AssetType::Folder) {
        info += "  \xC2\xB7  " + AssetDatabase::formatSize(entry.size);
    }
    drawList->AddText(ImVec2(textX + badgeWidth + 8.0f, min.y + 22.0f), ImGui::GetColorU32(kTextDim), info.c_str());
    EditorUI::drawTextEllipsis(drawList, ImVec2(textX, min.y + 38.0f), textWidth, entry.path.c_str(), kTextFaint);
    EditorUI::popFont();
    ImGui::Dummy(ImVec2(0.0f, tile + 8.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
}

void InspectorPanel::drawTextureAsset(const AssetEntry& entry) {
    if (!AssetDatabase::isLoadableTexture(entry.extension)) {
        EditorUI::iconLabel(ICON_LC_IMAGE_OFF, "The engine cannot load this image format", kWarning, kTextDim);
        if (EditorUI::beginProperties("##texture_info")) {
            EditorUI::propertyValue("File Size", AssetDatabase::formatSize(entry.size).c_str());
            EditorUI::endProperties();
        }
        return;
    }
    ThumbnailCache& thumbnails = ThumbnailCache::instance();
    const TextureData* texture = thumbnails.texture(entry.path);
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = std::min(width, 300.0f);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + width, min.y + height);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawChecker(drawList, min, max, 12.0f);
    if (texture) {
        drawImageFit(drawList, *texture, ImVec2(min.x + 6.0f, min.y + 6.0f), ImVec2(max.x - 6.0f, max.y - 6.0f), 4.0f);
    } else {
        ImGui::PushFont(nullptr, 26.0f);
        EditorUI::drawTextCentered(drawList, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f),
            thumbnails.isLoading(entry.path) ? ICON_LC_LOADER_CIRCLE : ICON_LC_IMAGE_OFF, kTextFaint);
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(width, height + 6.0f));
    if (EditorUI::beginProperties("##texture_info")) {
        if (texture) {
            EditorUI::propertyValue("Resolution", (std::to_string(texture->width) + " \xC3\x97 " + std::to_string(texture->height)).c_str());
            const char* channels = texture->channels == 4 ? "RGBA" : texture->channels == 3 ? "RGB" : texture->channels == 1 ? "R" : "Compressed";
            EditorUI::propertyValue("Channels", channels);
        } else {
            EditorUI::propertyValue("Status", thumbnails.isLoading(entry.path) ? "Loading..." : "Cannot decode this image");
        }
        EditorUI::propertyValue("File Size", AssetDatabase::formatSize(entry.size).c_str());
        EditorUI::endProperties();
    }
}

void InspectorPanel::drawLivePreview(EditorContext& context, const std::string& path) {
    if (previewPath_ != path) {
        previewPath_ = path;
        previewYaw_ = -0.62f;
        previewPitch_ = -0.32f;
        previewZoom_ = 1.0f;
    }
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = std::min(width, EditorUI::px(280.0f));
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + width, min.y + height);
    ImGui::InvisibleButton("##live_preview", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const ImGuiIO& io = ImGui::GetIO();
    // Орбита перетаскиванием, зум колесом, двойной клик — исходный ракурс.
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        previewYaw_ -= io.MouseDelta.x * 0.01f;
        previewPitch_ = std::clamp(previewPitch_ - io.MouseDelta.y * 0.01f, -1.4f, 1.4f);
    }
    if (hovered && io.MouseWheel != 0.0f) {
        previewZoom_ = std::clamp(previewZoom_ * (1.0f - io.MouseWheel * 0.1f), 0.4f, 3.0f);
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        previewYaw_ = -0.62f;
        previewPitch_ = -0.32f;
        previewZoom_ = 1.0f;
    }
    if (ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(IM_COL32(50, 51, 56, 255)), 8.0f);
    drawList->AddCircleFilled(ImVec2(min.x + width * 0.5f, min.y + height * 0.36f), height * 0.45f,
        ImGui::GetColorU32(IM_COL32(255, 255, 255, 9)), 64);
    const int pixelWidth = std::max(1, static_cast<int>(width * io.DisplayFramebufferScale.x));
    const int pixelHeight = std::max(1, static_cast<int>(height * io.DisplayFramebufferScale.y));
    const unsigned int texture = context.previewer.renderLive(path, pixelWidth, pixelHeight, previewYaw_, previewPitch_, previewZoom_, context.lastDt);
    if (texture != 0) {
        drawList->AddImageRounded(static_cast<ImTextureID>(texture), min, max, ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, 8.0f);
    } else {
        ImGui::PushFont(nullptr, 24.0f);
        EditorUI::drawTextCentered(drawList, ImVec2(min.x + width * 0.5f, min.y + height * 0.5f), ICON_LC_LOADER_CIRCLE, kTextFaint);
        ImGui::PopFont();
    }
    if (hovered && texture != 0) {
        EditorUI::pushSmallFont();
        const char* hint = ICON_LC_ROTATE_3D "  Drag to orbit  \xC2\xB7  scroll to zoom  \xC2\xB7  double-click to reset";
        const std::string fitted = EditorUI::ellipsize(hint, width - 16.0f);
        drawList->AddText(ImVec2(min.x + 8.0f, max.y - ImGui::GetFontSize() - 8.0f), ImGui::GetColorU32(IM_COL32(220, 220, 226, 170)), fitted.c_str());
        EditorUI::popFont();
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
}

void InspectorPanel::drawMaterialAsset(EditorContext& context, const AssetEntry& entry) {
    drawLivePreview(context, entry.path);
    MtlInfo info;
    if (!readMtl(entry.path, info)) {
        EditorUI::iconLabel(ICON_LC_CIRCLE_ALERT, "No materials in this file", kWarning, kTextDim);
        return;
    }
    EditorUI::pushSemibold();
    ImGui::TextUnformatted(info.materials.size() == 1 ? "Material" : "Materials");
    EditorUI::popFont();
    for (std::size_t i = 0; i < info.materials.size() && i < 64; ++i) {
        const MtlInfo::Entry& material = info.materials[i];
        ImGui::PushID(static_cast<int>(i));
        const float size = ImGui::GetFrameHeight();
        const ImVec2 swatch = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const TextureData* texture = material.diffuseTexture.empty() ? nullptr : ThumbnailCache::instance().texture(material.diffuseTexture);
        if (texture) {
            drawImageFit(drawList, *texture, swatch, ImVec2(swatch.x + size, swatch.y + size), 4.0f);
        } else {
            drawList->AddRectFilled(swatch, ImVec2(swatch.x + size, swatch.y + size),
                ImGui::ColorConvertFloat4ToU32(ImVec4(material.color.x, material.color.y, material.color.z, 1.0f)), 4.0f);
        }
        ImGui::Dummy(ImVec2(size, size));
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::BeginGroup();
        ImGui::TextUnformatted(material.name.c_str());
        EditorUI::pushSmallFont();
        EditorUI::textFaint(material.diffuseTexture.empty() ? "Color only" : fileName(material.diffuseTexture).c_str());
        EditorUI::popFont();
        ImGui::EndGroup();
        ImGui::PopID();
    }
    EditorUI::pushSmallFont();
    EditorUI::textFaint("Drag the material onto an object in the Scene to apply its base texture.");
    EditorUI::popFont();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

void InspectorPanel::drawModelAsset(EditorContext& context, const AssetEntry& entry) {
    drawLivePreview(context, entry.path);
    if (modelPath_ != entry.path) {
        modelPath_ = entry.path;
        modelPreview_ = ResourceManager::getInstance().loadMeshAsync(entry.path, JobPriority::Low);
    }
    if (!modelPreview_ || modelPreview_->isPending()) {
        ImGui::AlignTextToFramePadding();
        EditorUI::iconLabel(ICON_LC_LOADER_CIRCLE, "Importing model...", kTextDim, kTextDim);
    } else if (modelPreview_->isFailed() || modelPreview_->getData() == nullptr) {
        EditorUI::iconLabel(ICON_LC_CIRCLE_ALERT, "Import failed, see the Console", kError, kTextDim);
    } else {
        const MeshData& data = *modelPreview_->getData();
        std::size_t vertices = data.subMeshes.empty() ? data.vertices.size() : 0;
        std::size_t triangles = data.subMeshes.empty() ? data.indexCount / 3 : 0;
        for (const SubMesh& subMesh : data.subMeshes) {
            vertices += subMesh.vertices.size();
            triangles += subMesh.indexCount / 3;
        }
        if (EditorUI::beginProperties("##model_info")) {
            EditorUI::propertyValue("Submeshes", std::to_string(std::max<std::size_t>(1, data.subMeshes.size())).c_str());
            EditorUI::propertyValue("Vertices", std::to_string(vertices).c_str());
            EditorUI::propertyValue("Triangles", std::to_string(triangles).c_str());
            EditorUI::propertyValue("Skeleton", data.skeleton.nodes.empty() ? "None" : (std::to_string(data.skeleton.nodes.size()) + " nodes").c_str());
            EditorUI::endProperties();
        }
        if (!data.subMeshes.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            EditorUI::pushSemibold();
            ImGui::TextUnformatted("Materials");
            EditorUI::popFont();
            for (std::size_t i = 0; i < data.subMeshes.size() && i < 32; ++i) {
                const Material& material = data.subMeshes[i].material;
                const std::string name = material.name.empty() ? "Material " + std::to_string(i) : material.name;
                ImGui::PushID(static_cast<int>(i));
                const ImVec2 swatch = ImGui::GetCursorScreenPos();
                const float size = ImGui::GetTextLineHeight();
                ImGui::GetWindowDrawList()->AddRectFilled(swatch, ImVec2(swatch.x + size, swatch.y + size),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(material.diffuseColor.x, material.diffuseColor.y, material.diffuseColor.z, 1.0f)), 3.0f);
                ImGui::Dummy(ImVec2(size, size));
                ImGui::SameLine(0.0f, 8.0f);
                ImGui::TextUnformatted(name.c_str());
                if (!material.diffuseTexturePath.empty()) {
                    ImGui::SameLine();
                    EditorUI::textFaint(fileName(material.diffuseTexturePath).c_str());
                }
                ImGui::PopID();
            }
        }
        if (!data.skeleton.clips.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            EditorUI::pushSemibold();
            ImGui::TextUnformatted("Animation Clips");
            EditorUI::popFont();
            for (const AnimationClip& clip : data.skeleton.clips) {
                char duration[32];
                std::snprintf(duration, sizeof(duration), "%.2f s", clip.duration);
                EditorUI::iconLabel(ICON_LC_FILM, clip.name.c_str(), IM_COL32(196, 146, 255, 255));
                ImGui::SameLine();
                EditorUI::textFaint(duration);
            }
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::BeginDisabled(context.isPlaying());
    if (EditorUI::primaryButton(ICON_LC_PLUS "  Add to Scene", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() + 4.0f))) {
        const Vec3 spawn = context.dropPoint(context.camera.getPosition(), context.camera.getForward());
        context.createModel(entry.path, spawn);
    }
    ImGui::EndDisabled();
}

void InspectorPanel::drawSceneAsset(EditorContext& context, const AssetEntry& entry) {
    if (scenePath_ != entry.path || sceneModified_ != entry.modified) {
        scenePath_ = entry.path;
        sceneModified_ = entry.modified;
        SceneManifest manifest;
        sceneEntities_ = manifest.loadFromFile(entry.path) ? static_cast<int>(manifest.getEntities().size()) : -1;
    }
    if (EditorUI::beginProperties("##scene_info")) {
        EditorUI::propertyValue("Entities", sceneEntities_ >= 0 ? std::to_string(sceneEntities_).c_str() : "Invalid manifest");
        EditorUI::propertyValue("Open", entry.path == context.scenePath() ? "Yes, this is the current scene" : "No");
        EditorUI::endProperties();
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    if (EditorUI::primaryButton(ICON_LC_CLAPPERBOARD "  Open Scene", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() + 4.0f))) {
        context.loadScene(entry.path);
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    drawTextAsset(entry);
}

void InspectorPanel::drawAssetActions(EditorContext& context, const AssetEntry& entry) {
    (void)context;
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    const std::string reveal = std::string(ICON_LC_FOLDER_OPEN "  Show in ") + PlatformShell::fileManagerName();
    if (ImGui::Button(reveal.c_str())) {
        PlatformShell::revealInFileManager(entry.path);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_LC_COPY "  Copy Path")) {
        ImGui::SetClipboardText(entry.path.c_str());
    }
    if (entry.type != AssetType::Folder) {
        const bool luaScript = entry.type == AssetType::Script && entry.extension == ".lua";
        const char* openLabel = luaScript ? ICON_LC_EXTERNAL_LINK "  Open Externally" : ICON_LC_EXTERNAL_LINK "  Open";
        // Длинная подпись не лезет за край узкого инспектора: уходит на следующую строку.
        const ImGuiStyle& style = ImGui::GetStyle();
        const float needed = ImGui::CalcTextSize(openLabel).x + style.FramePadding.x * 2.0f;
        const float rightEdge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        if (rightEdge - ImGui::GetItemRectMax().x - style.ItemSpacing.x >= needed) {
            ImGui::SameLine();
        }
        if (ImGui::Button(openLabel)) {
            PlatformShell::openFile(entry.path);
        }
        if (luaScript) {
            ImGui::SetItemTooltip("Open the file in the default app for .lua files (an external editor).");
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
}

void InspectorPanel::drawTextAsset(const AssetEntry& entry) {
    const SyntaxLanguage language = entry.type == AssetType::Shader ? SyntaxLanguage::Glsl
        : (entry.type == AssetType::Json || entry.type == AssetType::Scene || entry.type == AssetType::Prefab) ? SyntaxLanguage::Json
        : entry.extension == ".lua" ? SyntaxLanguage::Lua : SyntaxLanguage::Plain;
    if (textPath_ != entry.path || textModified_ != entry.modified) {
        textPath_ = entry.path;
        textModified_ = entry.modified;
        textContent_ = readTextFile(entry.path, textTruncated_);
        // Табуляции — четыре пробела, иначе моноширинная колонка разъезжается.
        std::string expanded;
        expanded.reserve(textContent_.size());
        for (char c : textContent_) {
            if (c == '\t') {
                expanded += "    ";
            } else {
                expanded += c;
            }
        }
        textContent_ = std::move(expanded);
        // Начала строк и состояние блочных комментариев считаем один раз: клиппер просит произвольные диапазоны.
        lineStarts_.clear();
        lineInComment_.clear();
        const char* text = textContent_.c_str();
        const char* end = text + textContent_.size();
        lineStarts_.push_back(text);
        for (const char* p = text; p < end; ++p) {
            if (*p == '\n') {
                lineStarts_.push_back(p + 1);
            }
        }
        bool inComment = false;
        for (std::size_t i = 0; i < lineStarts_.size(); ++i) {
            lineInComment_.push_back(inComment);
            const char* lineEnd = i + 1 < lineStarts_.size() ? lineStarts_[i + 1] - 1 : end;
            advanceBlockComment(lineStarts_[i], lineEnd, language, inComment);
        }
        textLines_ = static_cast<int>(lineStarts_.size());
    }
    EditorUI::pushSmallFont();
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextFaint));
    ImGui::Text("%d lines%s", textLines_, textTruncated_ ? ", preview truncated" : "");
    ImGui::PopStyleColor();
    EditorUI::popFont();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, toVec4(IM_COL32(22, 22, 24, 255)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::BeginChild("##source", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_HorizontalScrollbar);
    EditorUI::pushMono();
    const char* end = textContent_.c_str() + textContent_.size();
    const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    const float gutter = ImGui::CalcTextSize("0000").x + 14.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    float widest = 0.0f;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(lineStarts_.size()), lineHeight);
    char number[16];
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const std::size_t index = static_cast<std::size_t>(i);
            const ImVec2 position = ImGui::GetCursorScreenPos();
            std::snprintf(number, sizeof(number), "%4d", i + 1);
            drawList->AddText(position, ImGui::GetColorU32(IM_COL32(82, 82, 90, 255)), number);
            const char* lineStart = lineStarts_[index];
            const char* lineEnd = index + 1 < lineStarts_.size() ? lineStarts_[index + 1] - 1 : end;
            bool inComment = lineInComment_[index];
            drawHighlightedLine(drawList, ImVec2(position.x + gutter, position.y), lineStart, std::max(lineStart, lineEnd), language, inComment);
            widest = std::max(widest, ImGui::CalcTextSize(lineStart, std::max(lineStart, lineEnd)).x);
            ImGui::Dummy(ImVec2(gutter + widest, lineHeight - ImGui::GetStyle().ItemSpacing.y));
        }
    }
    EditorUI::popFont();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}
