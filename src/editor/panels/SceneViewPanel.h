#pragma once

#include "math/MathTypes.h"

#include <imgui.h>

class EditorContext;

class SceneViewPanel {
public:
    void draw(EditorContext& context);

    bool open = true;

private:
    void drawToolbar(EditorContext& context);
    void handleShortcuts(EditorContext& context);
    void drawGizmo(EditorContext& context, const ImVec2& min, const ImVec2& size);
    bool drawCameraOverlays(EditorContext& context, const ImVec2& min, const ImVec2& size);
    void drawColliderOverlays(EditorContext& context, const ImVec2& min, const ImVec2& size);
    bool drawAxisGizmo(EditorContext& context, const ImVec2& min, const ImVec2& size);
    void drawStatusOverlay(EditorContext& context, const ImVec2& min);
    void handleDrop(EditorContext& context, const ImVec2& min, const ImVec2& size);
    bool mouseRay(EditorContext& context, const ImVec2& min, const ImVec2& size, Vec3& origin, Vec3& direction) const;

    bool hovered_ = false;
    bool focused_ = false;
    bool navigating_ = false;
    bool clickPending_ = false;
    ImVec2 clickStart_{};
};
