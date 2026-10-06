#include "editor/panels/SceneViewPanel.h"

#include "editor/EditorContext.h"
#include "editor/EditorIcons.h"
#include "editor/EditorMath.h"
#include "editor/EditorTheme.h"
#include "editor/EditorWidgets.h"
#include "editor/EditorWindows.h"
#include "editor/IconsLucide.h"
#include "math/CameraMath.h"
#include "render/IRenderAdapter.h"
#include "resources/ResourceManager.h"

#include <ImGuizmo.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

using namespace EditorTheme;
using namespace EditorMath;

namespace {
void applyGizmoStyle() {
    static bool applied = false;
    if (applied) {
        return;
    }
    applied = true;
    ImGuizmo::Style& style = ImGuizmo::GetStyle();
    style.TranslationLineThickness = 3.5f;
    style.TranslationLineArrowSize = 7.0f;
    style.RotationLineThickness = 3.0f;
    style.RotationOuterLineThickness = 3.5f;
    style.ScaleLineThickness = 3.5f;
    style.ScaleLineCircleSize = 7.0f;
    style.CenterCircleSize = 5.0f;
    style.Colors[ImGuizmo::DIRECTION_X] = toVec4(kAxisX);
    style.Colors[ImGuizmo::DIRECTION_Y] = toVec4(kAxisY);
    style.Colors[ImGuizmo::DIRECTION_Z] = toVec4(kAxisZ);
    style.Colors[ImGuizmo::PLANE_X] = toVec4(withAlpha(kAxisX, 0.38f));
    style.Colors[ImGuizmo::PLANE_Y] = toVec4(withAlpha(kAxisY, 0.38f));
    style.Colors[ImGuizmo::PLANE_Z] = toVec4(withAlpha(kAxisZ, 0.38f));
    style.Colors[ImGuizmo::SELECTION] = toVec4(IM_COL32(255, 214, 92, 255));
    style.Colors[ImGuizmo::ROTATION_USING_FILL] = toVec4(IM_COL32(255, 214, 92, 70));
    style.Colors[ImGuizmo::ROTATION_USING_BORDER] = toVec4(IM_COL32(255, 214, 92, 255));
}

Mat4 viewProjection(const EditorContext& context) {
    return Math::multiply(context.camera.getProjectionMatrix(), context.camera.getViewMatrix());
}

// Вид и проекция камеры сущности — как их считает Game View.
Mat4 entityCameraViewProjection(const EditorContext& context, Entity entity, float aspect) {
    const Camera& camera = context.world.getComponent<Camera>(entity);
    const Mat4 world = context.worldMatrix(entity);
    const Transform& transform = context.world.getComponent<Transform>(entity);
    const Mat4 view = CameraMath::view(transformPoint(world, {}), transform.rotation.x, transform.rotation.z);
    const float farClip = std::min(camera.farClip, 14.0f);
    const Mat4 projection = Math::perspective(toRadians(camera.fovDegrees), aspect, camera.nearClip, farClip);
    return Math::multiply(projection, view);
}

// Отрезок мира в экран с отсечением по ближней плоскости: иначе рёбра, уходящие за камеру, пропадают целиком.
bool projectSegment(const Mat4& viewProj, const Vec3& a, const Vec3& b, const ImVec2& min, const ImVec2& size, ImVec2& outA, ImVec2& outB) {
    const auto& m = viewProj.values;
    auto clip = [&m](const Vec3& p) {
        return std::array<float, 4>{
            m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
            m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14],
            m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15]};
    };
    std::array<float, 4> ca = clip(a);
    std::array<float, 4> cb = clip(b);
    constexpr float kNear = 0.01f;
    if (ca[3] < kNear && cb[3] < kNear) {
        return false;
    }
    if (ca[3] < kNear || cb[3] < kNear) {
        const float t = (kNear - ca[3]) / (cb[3] - ca[3]);
        std::array<float, 4> cut{};
        for (int i = 0; i < 4; ++i) {
            cut[i] = ca[i] + (cb[i] - ca[i]) * t;
        }
        (ca[3] < kNear ? ca : cb) = cut;
    }
    auto toScreen = [&](const std::array<float, 4>& c) {
        return ImVec2(min.x + (c[0] / c[3] * 0.5f + 0.5f) * size.x, min.y + (1.0f - (c[1] / c[3] * 0.5f + 0.5f)) * size.y);
    };
    outA = toScreen(ca);
    outB = toScreen(cb);
    return true;
}

bool toolShortcutAllowed() {
    const ImGuiIO& io = ImGui::GetIO();
    return !io.WantTextInput && !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
        !io.KeyCtrl && !io.KeyAlt && !io.KeyShift && !io.KeySuper &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !ImGuizmo::IsUsing();
}
}

void SceneViewPanel::draw(EditorContext& context) {
    context.sceneViewInputActive = false;
    // Превью перетаскиваемой модели живёт, только пока над вьюпортом; вкладку закрыли или спрятали — убираем.
    auto dropDragState = [&context]() {
        if (context.dragPreviewEntity != kInvalidEntity) {
            context.cancelDragPreview();
        }
        context.dropHighlight = kInvalidEntity;
    };
    if (!open) {
        dropDragState();
        return;
    }
    applyGizmoStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin(EditorWindow::kScene, &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!visible) {
        hovered_ = false;
        navigating_ = false;
        dropDragState();
        ImGui::End();
        return;
    }

    drawToolbar(context);

    const ImGuiIO& io = ImGui::GetIO();
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 1.0f);
    size.y = std::max(size.y, 1.0f);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + size.x, min.y + size.y);

    hovered_ = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(min, max);
    if (hovered_ && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) {
        ImGui::SetWindowFocus();
    }
    focused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // Начатый во вьюпорте облёт продолжается, даже если курсор ушёл за край окна.
    const bool anyNavigationButton = ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
        (io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left));
    if (hovered_ && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
            (io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))) && !ImGuizmo::IsOver()) {
        navigating_ = true;
    }
    if (!anyNavigationButton) {
        navigating_ = false;
    }
    const bool inputEnabled = (hovered_ || navigating_) && !io.WantTextInput && !ImGuizmo::IsUsing() && (navigating_ || !ImGuizmo::IsOver());
    context.sceneViewInputActive = inputEnabled;

    handleShortcuts(context);

    const int pixelWidth = std::max(1, static_cast<int>(size.x * io.DisplayFramebufferScale.x));
    const int pixelHeight = std::max(1, static_cast<int>(size.y * io.DisplayFramebufferScale.y));
    context.updateEditorCamera(context.lastDt, pixelWidth, pixelHeight, inputEnabled, hovered_ ? io.MouseWheel : 0.0f);
    context.renderSceneView(pixelWidth, pixelHeight);

    const unsigned int textureId = context.renderer.getViewportTextureId(kSceneViewTarget);
    ImGui::Image(static_cast<ImTextureID>(textureId), size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    handleDrop(context, min, size);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(min, max, true);
    drawColliderOverlays(context, min, size);
    bool clickConsumed = drawCameraOverlays(context, min, size);
    if (!context.isPlaying()) {
        drawGizmo(context, min, size);
    }
    clickConsumed |= drawAxisGizmo(context, min, size);
    drawStatusOverlay(context, min);
    drawList->PopClipRect();

    // Клик без перетаскивания — выбор объекта лучом из камеры.
    if (hovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt && !ImGuizmo::IsOver() && !clickConsumed) {
        clickPending_ = true;
        clickStart_ = io.MousePos;
    }
    if (clickPending_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        clickPending_ = false;
        const float dx = io.MousePos.x - clickStart_.x;
        const float dy = io.MousePos.y - clickStart_.y;
        Vec3 origin{};
        Vec3 direction{};
        if (dx * dx + dy * dy < 16.0f && !ImGuizmo::IsUsing() && mouseRay(context, min, size, origin, direction)) {
            const Entity hit = context.pick(origin, direction);
            if (hit != kInvalidEntity) {
                context.select(hit);
            } else {
                context.clearSelection();
            }
        }
    }
    ImGui::End();
}

void SceneViewPanel::drawToolbar(EditorContext& context) {
    SceneViewSettings& settings = context.sceneView;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight() + 10.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, ImVec2(min.x + width, min.y + height), ImGui::GetColorU32(kPanel));
    drawList->AddLine(ImVec2(min.x, min.y + height - 1.0f), ImVec2(min.x + width, min.y + height - 1.0f), ImGui::GetColorU32(kBorder));
    ImGui::SetCursorScreenPos(ImVec2(min.x + 8.0f, min.y + 5.0f));

    struct Tool {
        GizmoTool tool;
        const char* icon;
        const char* tooltip;
    };
    const Tool tools[] = {
        {GizmoTool::Select, ICON_LC_MOUSE_POINTER_2, "Select (Q)"},
        {GizmoTool::Translate, ICON_LC_MOVE, "Move (W)"},
        {GizmoTool::Rotate, ICON_LC_ROTATE_3D, "Rotate (E)"},
        {GizmoTool::Scale, ICON_LC_SCALE_3D, "Scale (R)"},
    };
    // Группа инструментов на общей подложке.
    {
        const float buttonSize = ImGui::GetFrameHeight();
        const ImVec2 groupMin = ImGui::GetCursorScreenPos();
        drawList->AddRectFilled(groupMin, ImVec2(groupMin.x + buttonSize * 4.0f + 6.0f, groupMin.y + buttonSize),
            ImGui::GetColorU32(kBackground), 5.0f);
        for (int i = 0; i < 4; ++i) {
            if (i > 0) {
                ImGui::SameLine(0.0f, 2.0f);
            }
            ImGui::PushID(i);
            if (EditorUI::iconButton("##tool", tools[i].icon, tools[i].tooltip, settings.tool == tools[i].tool)) {
                settings.tool = tools[i].tool;
            }
            ImGui::PopID();
        }
    }
    EditorUI::toolbarSeparator();
    if (EditorUI::toolButton("##space", settings.localSpace ? ICON_LC_BOX : ICON_LC_GLOBE, settings.localSpace ? "Local" : "Global",
            false, "Gizmo handle orientation (X)")) {
        settings.localSpace = !settings.localSpace;
    }
    ImGui::SameLine(0.0f, 4.0f);
    if (EditorUI::toolButton("##snap", ICON_LC_MAGNET, "Snap", settings.snap, "Snap while dragging (hold Ctrl to invert)")) {
        settings.snap = !settings.snap;
    }
    ImGui::SameLine(0.0f, 0.0f);
    if (EditorUI::iconButton("##snap_settings", ICON_LC_CHEVRON_DOWN, "Snap increments", false, ImGui::GetFrameHeight() * 0.8f)) {
        ImGui::OpenPopup("##snap_popup");
    }
    if (ImGui::BeginPopup("##snap_popup")) {
        EditorUI::pushSemibold();
        ImGui::TextUnformatted("Snap Increments");
        EditorUI::popFont();
        ImGui::Separator();
        if (EditorUI::beginProperties("##snap_props")) {
            EditorUI::propertyFloat("Move", settings.translateSnap, 0.01f, 0.01f, 100.0f, "%.2f", 0.25f);
            EditorUI::propertyFloat("Rotate", settings.rotateSnapDegrees, 0.5f, 1.0f, 180.0f, "%.0f\xC2\xB0", 15.0f);
            EditorUI::propertyFloat("Scale", settings.scaleSnap, 0.01f, 0.01f, 10.0f, "%.2f", 0.1f);
            EditorUI::endProperties();
        }
        ImGui::EndPopup();
    }
    EditorUI::toolbarSeparator();
    if (EditorUI::iconButton("##grid", ICON_LC_GRID_3X3, "Toggle grid", settings.showGrid)) {
        settings.showGrid = !settings.showGrid;
    }
    ImGui::SameLine(0.0f, 4.0f);
    bool colliders = context.debugRenderSystem.isEnabled();
    if (EditorUI::iconButton("##colliders", ICON_LC_SQUARE_DASHED, "Show colliders", colliders)) {
        context.debugRenderSystem.setEnabled(!colliders);
    }
    ImGui::SameLine(0.0f, 4.0f);
    if (EditorUI::toolButton("##overlays", ICON_LC_LAYERS, "Overlays", false, "Scene view overlays")) {
        ImGui::OpenPopup("##overlays_popup");
    }
    if (ImGui::BeginPopup("##overlays_popup")) {
        EditorUI::pushSemibold();
        ImGui::TextUnformatted("Overlays");
        EditorUI::popFont();
        ImGui::Separator();
        EditorUI::checkbox("Grid", &settings.showGrid);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        ImGui::DragFloat("##grid_height", &settings.gridHeight, 0.05f, -100.0f, 100.0f, "z = %.2f");
        bool showColliders = context.debugRenderSystem.isEnabled();
        if (EditorUI::checkbox("Colliders", &showColliders)) {
            context.debugRenderSystem.setEnabled(showColliders);
        }
        EditorUI::checkbox("Selection outline", &settings.showSelectionOutline);
        EditorUI::checkbox("Camera icons", &settings.showCameraIcons);
        EditorUI::checkbox("Statistics", &settings.showStats);
        ImGui::EndPopup();
    }

    // Справа: камера и «показать выделенное».
    const float rightWidth = ImGui::GetFrameHeight() * 2.0f + 70.0f;
    ImGui::SameLine(0.0f, 0.0f);
    const float rightX = std::max(ImGui::GetCursorScreenPos().x + 8.0f, min.x + width - rightWidth - 8.0f);
    ImGui::SetCursorScreenPos(ImVec2(rightX, min.y + 5.0f));
    char speed[24];
    std::snprintf(speed, sizeof(speed), "%.1f", context.camera.moveSpeed);
    if (EditorUI::toolButton("##camera", ICON_LC_VIDEO, speed, false, "Scene camera speed and field of view")) {
        ImGui::OpenPopup("##camera_popup");
    }
    if (ImGui::BeginPopup("##camera_popup")) {
        EditorUI::pushSemibold();
        ImGui::TextUnformatted("Scene Camera");
        EditorUI::popFont();
        ImGui::Separator();
        if (EditorUI::beginProperties("##camera_props")) {
            EditorUI::propertyLabel("Speed");
            ImGui::SliderFloat("##speed", &context.camera.moveSpeed, 0.5f, 40.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
            EditorUI::propertyLabel("Field of View");
            float fov = context.camera.getFov();
            if (ImGui::SliderFloat("##fov", &fov, 20.0f, 100.0f, "%.0f\xC2\xB0")) {
                context.camera.setFov(fov);
            }
            EditorUI::endProperties();
        }
        EditorUI::pushSmallFont();
        EditorUI::textFaint("Hold Shift while flying to move faster.");
        EditorUI::popFont();
        ImGui::EndPopup();
    }
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::BeginDisabled(!context.isEditable(context.selected));
    if (EditorUI::iconButton("##frame", ICON_LC_FOCUS, "Frame selected (F)")) {
        context.focusSelection();
    }
    ImGui::EndDisabled();

    ImGui::SetCursorScreenPos(ImVec2(min.x, min.y + height));
}

void SceneViewPanel::handleShortcuts(EditorContext& context) {
    if (!(focused_ || hovered_) || !toolShortcutAllowed()) {
        return;
    }
    SceneViewSettings& settings = context.sceneView;
    if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) settings.tool = GizmoTool::Select;
    if (ImGui::IsKeyPressed(ImGuiKey_W, false)) settings.tool = GizmoTool::Translate;
    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) settings.tool = GizmoTool::Rotate;
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) settings.tool = GizmoTool::Scale;
    if (ImGui::IsKeyPressed(ImGuiKey_X, false)) settings.localSpace = !settings.localSpace;
}

bool SceneViewPanel::mouseRay(EditorContext& context, const ImVec2& min, const ImVec2& size, Vec3& origin, Vec3& direction) const {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float localX = mouse.x - min.x;
    const float localY = mouse.y - min.y;
    if (size.x <= 1.0f || size.y <= 1.0f || localX < 0.0f || localY < 0.0f || localX > size.x || localY > size.y) {
        return false;
    }
    const float ndcX = (localX / size.x) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (localY / size.y) * 2.0f;
    origin = context.camera.getPosition();
    direction = context.camera.getRayDirection(ndcX, ndcY, size.x / size.y);
    return true;
}

void SceneViewPanel::drawGizmo(EditorContext& context, const ImVec2& min, const ImVec2& size) {
    const SceneViewSettings& settings = context.sceneView;
    if (settings.tool == GizmoTool::Select || !context.isEditable(context.selected) ||
        !context.world.hasComponent<Transform>(context.selected)) {
        return;
    }
    const Entity entity = context.selected;
    Mat4 model = context.worldMatrix(entity);

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::AllowAxisFlip(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(min.x, min.y, size.x, size.y);
    ImGuizmo::SetGizmoSizeClipSpace(0.12f);

    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    float snapValues[3] = {settings.translateSnap, settings.translateSnap, settings.translateSnap};
    if (settings.tool == GizmoTool::Rotate) {
        operation = ImGuizmo::ROTATE;
        snapValues[0] = snapValues[1] = snapValues[2] = settings.rotateSnapDegrees;
    } else if (settings.tool == GizmoTool::Scale) {
        operation = ImGuizmo::SCALE;
        snapValues[0] = snapValues[1] = snapValues[2] = settings.scaleSnap;
    }
    const bool snap = settings.snap != ImGui::GetIO().KeyCtrl;
    const ImGuizmo::MODE mode = (settings.localSpace || operation == ImGuizmo::SCALE) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    const Mat4 view = context.camera.getViewMatrix();
    const Mat4 projection = context.camera.getProjectionMatrix();
    if (!ImGuizmo::Manipulate(view.data(), projection.data(), operation, mode, model.data(), nullptr, snap ? snapValues : nullptr)) {
        return;
    }

    // Поворот (и любые правки потомка) берём из итоговой матрицы гизмо целиком: она уже повёрнута
    // вокруг нужной оси, а прибавлять углы к эйлеровым нельзя — движок применяет их в порядке Y·X·Z,
    // и после первого поворота кольца начинали крутить вокруг чужих осей.
    if (operation == ImGuizmo::ROTATE || context.parentOf(entity) != kInvalidEntity) {
        context.setWorldMatrix(entity, model);
        return;
    }
    // Перемещение и масштаб поворот не меняют — оставляем углы как есть, без пересчёта.
    Transform& transform = context.world.getComponent<Transform>(entity);
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{};
    decompose(model, position, rotation, scale);
    transform.position = position;
    if (operation == ImGuizmo::SCALE) {
        transform.scale = {std::max(0.01f, scale.x), std::max(0.01f, scale.y), std::max(0.01f, scale.z)};
    }
}

bool SceneViewPanel::drawCameraOverlays(EditorContext& context, const ImVec2& min, const ImVec2& size) {
    if (!context.sceneView.showCameraIcons) {
        return false;
    }
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const Mat4 viewProj = viewProjection(context);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    bool consumed = false;

    // Пирамида видимости выделенной камеры.
    if (context.isEditable(context.selected) && context.world.hasComponent<Camera>(context.selected) &&
        context.world.hasComponent<Transform>(context.selected)) {
        const Mat4 cameraViewProj = entityCameraViewProjection(context, context.selected, 16.0f / 9.0f);
        const Mat4 inverseViewProj = inverse(cameraViewProj);
        std::array<ImVec2, 8> corners{};
        std::array<bool, 8> valid{};
        for (int i = 0; i < 8; ++i) {
            const float x = (i & 1) ? 1.0f : -1.0f;
            const float y = (i & 2) ? 1.0f : -1.0f;
            const float z = (i & 4) ? 1.0f : -1.0f;
            const auto& m = inverseViewProj.values;
            const float wx = m[0] * x + m[4] * y + m[8] * z + m[12];
            const float wy = m[1] * x + m[5] * y + m[9] * z + m[13];
            const float wz = m[2] * x + m[6] * y + m[10] * z + m[14];
            const float ww = m[3] * x + m[7] * y + m[11] * z + m[15];
            const Vec3 world{wx / ww, wy / ww, wz / ww};
            valid[i] = projectToScreen(viewProj, world, min.x, min.y, size.x, size.y, corners[i].x, corners[i].y);
        }
        const int edges[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& edge : edges) {
            if (valid[edge[0]] && valid[edge[1]]) {
                drawList->AddLine(corners[edge[0]], corners[edge[1]], ImGui::GetColorU32(IM_COL32(255, 255, 255, 120)), 1.0f);
            }
        }
    }

    for (Entity entity : context.world.getEntities()) {
        if (!context.isEditable(entity) || !context.world.hasComponent<Camera>(entity)) {
            continue;
        }
        float x = 0.0f;
        float y = 0.0f;
        if (!projectToScreen(viewProj, transformPoint(context.worldMatrix(entity), {}), min.x, min.y, size.x, size.y, x, y)) {
            continue;
        }
        const float radius = 13.0f;
        const bool hovered = hovered_ && (mouse.x - x) * (mouse.x - x) + (mouse.y - y) * (mouse.y - y) <= radius * radius;
        const bool selected = context.selected == entity;
        drawList->AddCircleFilled(ImVec2(x, y), radius, ImGui::GetColorU32(hovered ? IM_COL32(58, 58, 64, 235) : IM_COL32(30, 30, 33, 220)), 24);
        drawList->AddCircle(ImVec2(x, y), radius, ImGui::GetColorU32(selected ? kSelection : IM_COL32(255, 255, 255, 60)), 24, selected ? 2.0f : 1.0f);
        EditorUI::drawTextCentered(drawList, ImVec2(x, y), ICON_LC_VIDEO, entity == context.gameCameraEntity ? IM_COL32(140, 190, 255, 255) : kText);
        if (hovered) {
            ImGui::SetTooltip("%s", context.displayName(entity).c_str());
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                context.select(entity);
                consumed = true;
            }
        }
    }
    return consumed;
}

void SceneViewPanel::drawColliderOverlays(EditorContext& context, const ImVec2& min, const ImVec2& size) {
    if (!context.debugRenderSystem.isEnabled()) {
        return;
    }
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const Mat4 viewProj = viewProjection(context);
    auto line = [&](const Vec3& a, const Vec3& b, ImU32 color, float thickness) {
        ImVec2 sa;
        ImVec2 sb;
        if (projectSegment(viewProj, a, b, min, size, sa, sb)) {
            drawList->AddLine(sa, sb, color, thickness);
        }
    };
    for (Entity entity : context.world.getEntities()) {
        if (!context.isEditable(entity) || !context.world.hasComponent<Collider>(entity) || !context.world.hasComponent<Transform>(entity)) {
            continue;
        }
        const bool selected = entity == context.selected;
        const ImU32 color = ImGui::GetColorU32(selected ? IM_COL32(170, 255, 160, 255) : IM_COL32(130, 236, 124, 170));
        const float thickness = selected ? 1.8f : 1.2f;
        Transform world;
        decompose(context.worldMatrix(entity), world.position, world.rotation, world.scale);
        const Collider& collider = context.world.getComponent<Collider>(entity);
        if (collider.type == ColliderType::Box) {
            // Тот же AABB, что считает физика.
            const AABB box = CollisionUtils::buildAABB(world, collider);
            Vec3 corners[8];
            for (int i = 0; i < 8; ++i) {
                corners[i] = {box.center.x + ((i & 1) ? box.halfSize.x : -box.halfSize.x),
                    box.center.y + ((i & 2) ? box.halfSize.y : -box.halfSize.y),
                    box.center.z + ((i & 4) ? box.halfSize.z : -box.halfSize.z)};
            }
            const int edges[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (const auto& edge : edges) {
                line(corners[edge[0]], corners[edge[1]], color, thickness);
            }
        } else {
            const Sphere sphere = CollisionUtils::buildSphere(world, collider);
            constexpr int kSegments = 40;
            for (int plane = 0; plane < 3; ++plane) {
                Vec3 previous{};
                for (int i = 0; i <= kSegments; ++i) {
                    const float angle = 2.0f * kPi * static_cast<float>(i) / kSegments;
                    const float c = std::cos(angle) * sphere.radius;
                    const float s = std::sin(angle) * sphere.radius;
                    const Vec3 offset = plane == 0 ? Vec3{c, s, 0.0f} : plane == 1 ? Vec3{c, 0.0f, s} : Vec3{0.0f, c, s};
                    const Vec3 point = add(sphere.center, offset);
                    if (i > 0) {
                        line(previous, point, color, thickness);
                    }
                    previous = point;
                }
            }
        }
    }
}

bool SceneViewPanel::drawAxisGizmo(EditorContext& context, const ImVec2& min, const ImVec2& size) {
    const float radius = 38.0f;
    const ImVec2 center(min.x + size.x - radius - 18.0f, min.y + radius + 16.0f);
    if (size.x < radius * 4.0f || size.y < radius * 3.0f) {
        return false;
    }
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const auto& v = context.camera.getViewMatrix().values;

    struct Handle {
        Vec3 axis;
        ImU32 color;
        const char* label;
        bool positive;
        float depth;
        ImVec2 position;
    };
    std::array<Handle, 6> handles{};
    const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const ImU32 colors[3] = {kAxisX, kAxisY, kAxisZ};
    const char* labels[3] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i) {
        for (int sign = 0; sign < 2; ++sign) {
            const float s = sign == 0 ? 1.0f : -1.0f;
            const Vec3 axis = mul(axes[i], s);
            const float vx = v[0] * axis.x + v[4] * axis.y + v[8] * axis.z;
            const float vy = v[1] * axis.x + v[5] * axis.y + v[9] * axis.z;
            const float vz = v[2] * axis.x + v[6] * axis.y + v[10] * axis.z;
            handles[static_cast<std::size_t>(i * 2 + sign)] = Handle{axis, colors[i], labels[i], sign == 0, vz,
                ImVec2(center.x + vx * radius * 0.78f, center.y - vy * radius * 0.78f)};
        }
    }
    const bool areaHovered = hovered_ && (mouse.x - center.x) * (mouse.x - center.x) + (mouse.y - center.y) * (mouse.y - center.y) <= radius * radius;
    if (areaHovered) {
        drawList->AddCircleFilled(center, radius, ImGui::GetColorU32(IM_COL32(255, 255, 255, 18)), 40);
    }
    std::sort(handles.begin(), handles.end(), [](const Handle& a, const Handle& b) { return a.depth < b.depth; });

    int hoveredHandle = -1;
    for (int i = static_cast<int>(handles.size()) - 1; i >= 0; --i) {
        const Handle& handle = handles[static_cast<std::size_t>(i)];
        const float r = handle.positive ? 9.0f : 7.0f;
        if (hovered_ && (mouse.x - handle.position.x) * (mouse.x - handle.position.x) + (mouse.y - handle.position.y) * (mouse.y - handle.position.y) <= (r + 2.0f) * (r + 2.0f)) {
            hoveredHandle = i;
            break;
        }
    }
    for (int i = 0; i < static_cast<int>(handles.size()); ++i) {
        const Handle& handle = handles[static_cast<std::size_t>(i)];
        const bool hovered = i == hoveredHandle;
        if (handle.positive) {
            drawList->AddLine(center, handle.position, ImGui::GetColorU32(withAlpha(handle.color, 0.9f)), 2.0f);
            drawList->AddCircleFilled(handle.position, 9.0f, ImGui::GetColorU32(hovered ? mix(handle.color, IM_COL32_WHITE, 0.35f) : handle.color), 20);
            ImGui::PushFont(fonts().semibold, 11.0f);
            EditorUI::drawTextCentered(drawList, ImVec2(handle.position.x, handle.position.y + 0.5f), handle.label, IM_COL32(20, 20, 22, 255));
            ImGui::PopFont();
        } else {
            drawList->AddCircleFilled(handle.position, 7.0f, ImGui::GetColorU32(withAlpha(handle.color, hovered ? 0.7f : 0.3f)), 20);
            drawList->AddCircle(handle.position, 7.0f, ImGui::GetColorU32(withAlpha(handle.color, 0.9f)), 20, 1.2f);
        }
        if (hovered) {
            drawList->AddCircle(handle.position, handle.positive ? 10.5f : 8.5f, ImGui::GetColorU32(IM_COL32(255, 255, 255, 200)), 20, 1.5f);
        }
    }
    if (hoveredHandle >= 0) {
        const Handle& handle = handles[static_cast<std::size_t>(hoveredHandle)];
        ImGui::SetTooltip("View from %s%s", handle.positive ? "+" : "-", handle.label);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            // Взгляд «с оси» — камера смотрит против неё, на опорную точку.
            context.camera.lookAlong(mul(handle.axis, -1.0f));
            return true;
        }
    }
    return areaHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

void SceneViewPanel::drawStatusOverlay(EditorContext& context, const ImVec2& min) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    float y = min.y + 10.0f;
    if (context.isPlaying()) {
        const char* text = context.paused ? ICON_LC_PAUSE "  Paused" : ICON_LC_PLAY "  Playing";
        EditorUI::pushSemibold();
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const ImVec2 pillMin(min.x + 10.0f, y);
        const ImVec2 pillMax(pillMin.x + textSize.x + 20.0f, pillMin.y + textSize.y + 8.0f);
        drawList->AddRectFilled(pillMin, pillMax, ImGui::GetColorU32(context.paused ? withAlpha(kWarning, 0.9f) : withAlpha(kAccent, 0.92f)), 12.0f);
        drawList->AddText(ImVec2(pillMin.x + 10.0f, pillMin.y + 4.0f), ImGui::GetColorU32(IM_COL32_WHITE), text);
        EditorUI::popFont();
        y = pillMax.y + 8.0f;
    }
    if (!context.sceneView.showStats) {
        return;
    }
    ResourceManager& resources = ResourceManager::getInstance();
    char lines[4][64];
    std::snprintf(lines[0], sizeof(lines[0]), "%.0f FPS  \xC2\xB7  %.2f ms", context.fpsAverage, context.frameTimeAverageMs);
    std::snprintf(lines[1], sizeof(lines[1]), "Meshes drawn  %zu", context.sceneDrawnMeshes);
    std::snprintf(lines[2], sizeof(lines[2]), "Entities  %zu", context.world.getEntityCount() - 1);
    std::snprintf(lines[3], sizeof(lines[3]), "Loads pending  %zu", resources.pendingLoadCount());
    EditorUI::pushSmallFont();
    // Ширина по шаблону, а не по тексту: рамка не прыгает вместе с цифрами.
    float width = ImGui::CalcTextSize("0000 FPS  \xC2\xB7  000.00 ms").x;
    for (const auto& line : lines) {
        width = std::max(width, ImGui::CalcTextSize(line).x);
    }
    const float lineHeight = ImGui::GetTextLineHeight() + 2.0f;
    const ImVec2 boxMin(min.x + 10.0f, y);
    const ImVec2 boxMax(boxMin.x + width + 20.0f, boxMin.y + lineHeight * 4.0f + 12.0f);
    drawList->AddRectFilled(boxMin, boxMax, ImGui::GetColorU32(IM_COL32(16, 16, 18, 200)), 6.0f);
    for (int i = 0; i < 4; ++i) {
        drawList->AddText(ImVec2(boxMin.x + 10.0f, boxMin.y + 6.0f + lineHeight * i), ImGui::GetColorU32(i == 0 ? kText : kTextDim), lines[i]);
    }
    EditorUI::popFont();
}

void SceneViewPanel::handleDrop(EditorContext& context, const ImVec2& min, const ImVec2& size) {
    // Что сейчас тащат из Content Browser и где курсор — для превью модели и подсветки цели.
    const ImGuiPayload* dragging = ImGui::GetDragDropPayload();
    std::string draggedPath;
    AssetType draggedType = AssetType::Other;
    if (dragging && dragging->IsDataType(kAssetPayload) && dragging->Data) {
        draggedPath = static_cast<const char*>(dragging->Data);
        draggedType = AssetDatabase::classify(draggedPath);
    }
    const ImVec2 max(min.x + size.x, min.y + size.y);
    const bool overViewport = !draggedPath.empty() && ImGui::IsMouseHoveringRect(min, max) &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    Vec3 origin{};
    Vec3 direction{};
    const bool hasRay = overViewport && mouseRay(context, min, size, origin, direction);
    const bool applies = draggedType == AssetType::Texture || draggedType == AssetType::Material;

    bool dropped = false;
    if (ImGui::BeginDragDropTarget()) {
        const char* hint = nullptr;
        if (context.isPlaying() && draggedType != AssetType::Scene) {
            hint = ICON_LC_BAN "  Stop Play mode to edit the scene";
        } else if (draggedType == AssetType::Scene) {
            hint = ICON_LC_CLAPPERBOARD "  Open this scene";
        } else if (applies) {
            hint = context.dropHighlight != kInvalidEntity ? ICON_LC_BRUSH "  Apply to the highlighted object"
                                                           : ICON_LC_BRUSH "  Drop on an object to apply";
        }
        if (hint) {
            ImGui::SetTooltip("%s", hint);
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
            dropped = true;
            const std::string path(static_cast<const char*>(payload->Data));
            const AssetType type = AssetDatabase::classify(path);
            if (type == AssetType::Scene) {
                context.loadScene(path);
            } else if (!context.isPlaying() && type == AssetType::Model) {
                if (context.commitDragPreview() == kInvalidEntity && hasRay) {
                    context.createModel(path, context.dropPoint(origin, direction));
                }
            } else if (!context.isPlaying() && applies && hasRay) {
                const Entity hit = context.pick(origin, direction);
                if (context.applyAssetToEntity(path, hit)) {
                    context.select(hit);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Модель живёт в сцене, пока курсор над вьюпортом; ушёл или отпустил мимо — убираем.
    if (!dropped && hasRay && draggedType == AssetType::Model && !context.isPlaying()) {
        context.updateDragPreview(draggedPath, origin, direction);
    } else if (!dropped && context.dragPreviewEntity != kInvalidEntity) {
        context.cancelDragPreview();
    }
    context.dropHighlight = (!dropped && hasRay && applies && !context.isPlaying()) ? context.pick(origin, direction) : kInvalidEntity;
    if (context.dropHighlight != kInvalidEntity && !context.world.hasComponent<MeshRenderer>(context.dropHighlight)) {
        context.dropHighlight = kInvalidEntity;
    }
}
