#include "editor/panels/HierarchyPanel.h"

#include "editor/EditorContext.h"
#include "editor/EditorIcons.h"
#include "editor/EditorTheme.h"
#include "editor/EditorWidgets.h"
#include "editor/EditorWindows.h"
#include "editor/IconsLucide.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

using namespace EditorTheme;

namespace {
std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

void dragPreview(EditorContext& context, Entity entity) {
    ImU32 color = 0;
    const char* icon = entityIcon(context, entity, &color);
    EditorUI::iconLabel(icon, context.displayName(entity).c_str(), color);
}
}

void HierarchyPanel::draw(EditorContext& context) {
    if (!open) {
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin(EditorWindow::kHierarchy, &open);
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return;
    }

    // Выделение сменилось (например, кликом во вьюпорте) — раскрываем родителей и прокручиваем к объекту.
    // Только в этот кадр, иначе родителя выделенного нельзя было бы свернуть.
    revealSelection_ = context.selected != lastSelected_;
    if (revealSelection_) {
        scrollTo_ = context.selected;
        lastSelected_ = context.selected;
    }

    // Шапка: поиск и кнопка «создать».
    ImGui::SetCursorPos(ImVec2(8.0f, ImGui::GetCursorPosY() + 6.0f));
    const float addSize = ImGui::GetFrameHeight();
    EditorUI::searchBox("hierarchy_search", search_.data(), search_.size(), "Search", ImGui::GetContentRegionAvail().x - addSize - 14.0f);
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::BeginDisabled(context.isPlaying());
    if (EditorUI::iconButton("##create", ICON_LC_PLUS, "Create")) {
        ImGui::OpenPopup("##create_popup");
    }
    if (ImGui::BeginPopup("##create_popup")) {
        createMenuItems(context, kInvalidEntity);
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
    ImGui::GetWindowDrawList()->AddLine(ImGui::GetCursorScreenPos(),
        ImVec2(ImGui::GetCursorScreenPos().x + ImGui::GetWindowWidth(), ImGui::GetCursorScreenPos().y), ImGui::GetColorU32(kBorder));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
    ImGui::BeginChild("##hierarchy_tree", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 16.0f);

    if (search_[0] != '\0') {
        drawSearchResults(context);
    } else {
        // Корень сцены: имя сцены, сюда же бросают сущность, чтобы отцепить её от родителя.
        const ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth |
            ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DrawLinesToNodes;
        const float labelX = ImGui::GetCursorScreenPos().x + ImGui::GetTreeNodeToLabelSpacing();
        const bool rootOpen = ImGui::TreeNodeEx("##scene_root", rootFlags);
        acceptDrop(context, kInvalidEntity);
        {
            const ImVec2 min = ImGui::GetItemRectMin();
            const float textY = min.y + (ImGui::GetItemRectSize().y - ImGui::GetFontSize()) * 0.5f;
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddText(ImVec2(labelX, textY), ImGui::GetColorU32(kAssetScene), ICON_LC_CLAPPERBOARD);
            ImGui::PushFont(fonts().semibold, 0.0f);
            drawList->AddText(ImVec2(labelX + ImGui::GetFontSize() + EditorUI::px(8.0f), textY), ImGui::GetColorU32(kText), context.sceneName().c_str());
            ImGui::PopFont();
        }
        if (rootOpen) {
            for (Entity entity : context.rootEntities()) {
                drawEntity(context, entity, 0);
            }
            ImGui::TreePop();
        }
    }

    ImGui::PopStyleVar(3);

    // Пустое место под деревом: снять выделение, бросить сущность в корень, контекстное меню.
    const ImVec2 rest = ImGui::GetContentRegionAvail();
    if (rest.y > 4.0f) {
        ImGui::InvisibleButton("##empty_area", ImVec2(std::max(rest.x, 1.0f), rest.y));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            context.clearSelection();
        }
        acceptDrop(context, kInvalidEntity);
        if (ImGui::BeginPopupContextItem("##hierarchy_blank")) {
            ImGui::BeginDisabled(context.isPlaying());
            createMenuItems(context, kInvalidEntity);
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void HierarchyPanel::createMenuItems(EditorContext& context, Entity parent) {
    const Vec3 spawn = parent != kInvalidEntity ? Vec3{} : context.defaultSpawnPosition();
    Entity created = kInvalidEntity;
    if (ImGui::MenuItem(ICON_LC_SQUARE_DASHED "  Create Empty")) {
        created = context.createEmpty("Empty", spawn);
    }
    if (ImGui::MenuItem(ICON_LC_BOX "  Cube")) {
        created = context.createCube("Cube", spawn);
    }
    if (created != kInvalidEntity && parent != kInvalidEntity) {
        context.setParent(created, parent);
        if (context.world.hasComponent<Transform>(created)) {
            context.world.getComponent<Transform>(created).position = Vec3{};
        }
    }
    if (created != kInvalidEntity) {
        context.beginRename(created);
    }
}

void HierarchyPanel::acceptDrop(EditorContext& context, Entity target) {
    if (!ImGui::BeginDragDropTarget()) {
        return;
    }
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityPayload)) {
        const Entity dragged = *static_cast<const Entity*>(payload->Data);
        if (!context.isPlaying()) {
            context.setParent(dragged, target);
            context.select(dragged);
        }
    }
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetPayload)) {
        const std::string path(static_cast<const char*>(payload->Data));
        if (!context.isPlaying() && AssetDatabase::classify(path) == AssetType::Model) {
            const Entity created = context.createModel(path, context.defaultSpawnPosition());
            if (created != kInvalidEntity && target != kInvalidEntity) {
                context.setParent(created, target);
            }
        }
    }
    ImGui::EndDragDropTarget();
}

void HierarchyPanel::drawRowContents(EditorContext& context, Entity entity, float labelX, bool showParent) {
    const ImVec2 rowMin = ImGui::GetItemRectMin();
    const ImVec2 rowMax = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const bool selected = context.selected == entity;
    if (selected) {
        drawList->AddRectFilled(ImVec2(rowMin.x, rowMin.y + 1.0f), ImVec2(rowMin.x + 2.0f, rowMax.y - 1.0f), ImGui::GetColorU32(kAccent));
    }

    ImU32 iconColor = 0;
    const char* icon = entityIcon(context, entity, &iconColor);
    const float textY = rowMin.y + (rowMax.y - rowMin.y - ImGui::GetFontSize()) * 0.5f;
    bool hidden = false;
    if (context.world.hasComponent<MeshRenderer>(entity)) {
        hidden = !context.world.getComponent<MeshRenderer>(entity).visible;
    }
    drawList->AddText(ImVec2(labelX, textY), ImGui::GetColorU32(hidden ? withAlpha(iconColor, 0.45f) : iconColor), icon);
    const float nameX = labelX + ImGui::CalcTextSize(icon).x + 7.0f;

    // Видимость: глаз справа — при наведении или если объект скрыт.
    const bool hasMesh = context.world.hasComponent<MeshRenderer>(entity);
    const bool rowHovered = ImGui::IsMouseHoveringRect(rowMin, rowMax);
    const float eyeSize = rowMax.y - rowMin.y;
    float nameRight = rowMax.x - 6.0f;
    if (hasMesh && (rowHovered || hidden)) {
        nameRight -= eyeSize;
    }

    if (context.renamingEntity == entity) {
        ImGui::SetCursorScreenPos(ImVec2(nameX - 4.0f, rowMin.y + 1.0f));
        ImGui::SetNextItemWidth(std::max(40.0f, nameRight - nameX));
        if (context.renameNeedsFocus) {
            ImGui::SetKeyboardFocusHere();
            context.renameNeedsFocus = false;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 2.0f));
        const bool submitted = ImGui::InputText("##rename", context.renameBuffer.data(), context.renameBuffer.size(),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleVar();
        if (submitted || ImGui::IsItemDeactivated()) {
            context.commitRename();
        }
    } else {
        const std::string name = context.displayName(entity);
        ImU32 textColor = hidden ? kTextFaint : kText;
        if (context.isPlaying() && !selected) {
            textColor = hidden ? kTextFaint : IM_COL32(210, 210, 214, 255);
        }
        EditorUI::drawTextEllipsis(drawList, ImVec2(nameX, textY), nameRight - nameX, name.c_str(), textColor);
        if (showParent) {
            const Entity parent = context.parentOf(entity);
            if (parent != kInvalidEntity) {
                const float nameWidth = ImGui::CalcTextSize(name.c_str()).x;
                const std::string parentLabel = "in " + context.displayName(parent);
                EditorUI::drawTextEllipsis(drawList, ImVec2(nameX + nameWidth + 8.0f, textY),
                    nameRight - nameX - nameWidth - 8.0f, parentLabel.c_str(), kTextFaint);
            }
        }
    }

    if (hasMesh && (rowHovered || hidden)) {
        ImGui::SetCursorScreenPos(ImVec2(rowMax.x - eyeSize - 2.0f, rowMin.y));
        ImGui::PushID(static_cast<int>(entity));
        MeshRenderer& meshRenderer = context.world.getComponent<MeshRenderer>(entity);
        if (EditorUI::iconButton("##visible", meshRenderer.visible ? ICON_LC_EYE : ICON_LC_EYE_OFF,
                meshRenderer.visible ? "Hide" : "Show", false, eyeSize)) {
            meshRenderer.visible = !meshRenderer.visible;
        }
        ImGui::PopID();
    }
}

void HierarchyPanel::entityContextMenu(EditorContext& context, Entity entity) {
    if (!ImGui::BeginPopupContextItem("##entity_context")) {
        return;
    }
    context.select(entity);
    ImGui::BeginDisabled(context.isPlaying());
    if (ImGui::MenuItem(ICON_LC_PENCIL "  Rename", "F2")) {
        context.beginRename(entity);
    }
    if (ImGui::MenuItem(ICON_LC_COPY "  Duplicate", EditorUI::shortcut("Ctrl+D").c_str())) {
        context.duplicate(entity);
    }
    if (ImGui::MenuItem(ICON_LC_TRASH_2 "  Delete", "Del")) {
        context.destroy(entity);
    }
    ImGui::Separator();
    if (ImGui::BeginMenu(ICON_LC_PLUS "  Create Child")) {
        createMenuItems(context, entity);
        ImGui::EndMenu();
    }
    if (context.parentOf(entity) != kInvalidEntity && ImGui::MenuItem(ICON_LC_UNGROUP "  Unparent")) {
        context.setParent(entity, kInvalidEntity);
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_LC_FOCUS "  Frame Selected", "F")) {
        context.focusSelection();
    }
    ImGui::EndPopup();
}

void HierarchyPanel::drawEntity(EditorContext& context, Entity entity, int depth) {
    if (!context.isEditable(entity) || depth > 64) {
        return;
    }
    const std::vector<Entity> children = context.childrenOf(entity);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_DrawLinesToNodes;
    if (children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (context.selected == entity) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (revealSelection_ && !children.empty() && context.selected != kInvalidEntity && context.isAncestor(entity, context.selected)) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }

    ImGui::PushID(static_cast<int>(entity));
    const float labelX = ImGui::GetCursorScreenPos().x + ImGui::GetTreeNodeToLabelSpacing();
    ImGui::SetNextItemAllowOverlap();
    const bool nodeOpen = ImGui::TreeNodeEx("##node", flags);
    if (scrollTo_ == entity) {
        ImGui::SetScrollHereY(0.4f);
        scrollTo_ = kInvalidEntity;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
        context.select(entity);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && children.empty()) {
        context.focusSelection();
    }
    if (!context.isPlaying() && ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(kEntityPayload, &entity, sizeof(Entity));
        dragPreview(context, entity);
        ImGui::EndDragDropSource();
    }
    acceptDrop(context, entity);
    entityContextMenu(context, entity);

    const ImVec2 cursorAfter = ImGui::GetCursorScreenPos();
    drawRowContents(context, entity, labelX, false);
    ImGui::SetCursorScreenPos(cursorAfter);

    if (nodeOpen && !children.empty()) {
        for (Entity child : children) {
            drawEntity(context, child, depth + 1);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void HierarchyPanel::drawSearchResults(EditorContext& context) {
    const std::string needle = lower(search_.data());
    int matches = 0;
    for (Entity entity : context.world.getEntities()) {
        if (!context.isEditable(entity) || lower(context.displayName(entity)).find(needle) == std::string::npos) {
            continue;
        }
        ++matches;
        ImGui::PushID(static_cast<int>(entity));
        const float labelX = ImGui::GetCursorScreenPos().x + ImGui::GetTreeNodeToLabelSpacing();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
            ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding;
        if (context.selected == entity) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        ImGui::SetNextItemAllowOverlap();
        ImGui::TreeNodeEx("##match", flags);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            context.select(entity);
        }
        entityContextMenu(context, entity);
        const ImVec2 cursorAfter = ImGui::GetCursorScreenPos();
        drawRowContents(context, entity, labelX, true);
        ImGui::SetCursorScreenPos(cursorAfter);
        ImGui::PopID();
    }
    if (matches == 0) {
        ImGui::Dummy(ImVec2(0.0f, 12.0f));
        EditorUI::emptyState(ICON_LC_SEARCH, "No matches", "Try another name");
    }
}
