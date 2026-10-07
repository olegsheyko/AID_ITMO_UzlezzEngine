#include "editor/panels/GameViewPanel.h"

#include "editor/EditorContext.h"
#include "editor/EditorTheme.h"
#include "editor/EditorWidgets.h"
#include "editor/EditorWindows.h"
#include "editor/IconsLucide.h"
#include "render/IRenderAdapter.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

using namespace EditorTheme;

namespace {
struct Resolution {
    const char* label;
    int width;   // 0 — свободное разрешение по размеру окна
    int height;  // для пресетов соотношения сторон — 0, а width/height хранят пропорцию в aspectX/aspectY
    float aspectX;
    float aspectY;
};

const Resolution kResolutions[] = {
    {"Free Aspect", 0, 0, 0.0f, 0.0f},
    {"16:9 Aspect", 0, 0, 16.0f, 9.0f},
    {"16:10 Aspect", 0, 0, 16.0f, 10.0f},
    {"4:3 Aspect", 0, 0, 4.0f, 3.0f},
    {"Full HD (1920\xC3\x97" "1080)", 1920, 1080, 16.0f, 9.0f},
    {"HD (1280\xC3\x97" "720)", 1280, 720, 16.0f, 9.0f},
    {"Portrait (1080\xC3\x97" "1920)", 1080, 1920, 9.0f, 16.0f},
};
}

void GameViewPanel::drawToolbar(EditorContext& context) {
    (void)context;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight() + 10.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, ImVec2(min.x + width, min.y + height), ImGui::GetColorU32(kPanel));
    drawList->AddLine(ImVec2(min.x, min.y + height - 1.0f), ImVec2(min.x + width, min.y + height - 1.0f), ImGui::GetColorU32(kBorder));
    ImGui::SetCursorScreenPos(ImVec2(min.x + 8.0f, min.y + 5.0f));

    ImGui::AlignTextToFramePadding();
    EditorUI::textFaint(ICON_LC_MONITOR);
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::SetNextItemWidth(190.0f);
    resolution = std::clamp(resolution, 0, static_cast<int>(std::size(kResolutions)) - 1);
    if (EditorUI::beginCombo("##resolution", kResolutions[resolution].label)) {
        for (int i = 0; i < static_cast<int>(std::size(kResolutions)); ++i) {
            if (i == 4) {
                ImGui::Separator();
            }
            if (ImGui::Selectable(kResolutions[i].label, resolution == i)) {
                resolution = i;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorScreenPos(ImVec2(min.x + width - ImGui::GetFrameHeight() - 8.0f, min.y + 5.0f));
    if (EditorUI::iconButton("##stats", ICON_LC_CHART_NO_AXES_COLUMN, "Statistics", showStats)) {
        showStats = !showStats;
    }
    ImGui::SetCursorScreenPos(ImVec2(min.x, min.y + height));
}

void GameViewPanel::draw(EditorContext& context) {
    context.gameViewInputActive = false;
    context.gameViewFocused = false;
    if (!open) {
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin(EditorWindow::kGame, &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return;
    }
    drawToolbar(context);

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 available(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y));
    const ImVec2 areaMin = ImGui::GetCursorScreenPos();
    const ImVec2 areaMax(areaMin.x + available.x, areaMin.y + available.y);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(areaMin, areaMax, ImGui::GetColorU32(IM_COL32(12, 12, 13, 255)));

    // Картинка вписывается в окно с сохранением пропорций выбранного пресета.
    const Resolution& preset = kResolutions[resolution];
    ImVec2 imageSize = available;
    if (preset.aspectX > 0.0f) {
        const float aspect = preset.aspectX / preset.aspectY;
        imageSize = available.x / available.y > aspect ? ImVec2(available.y * aspect, available.y) : ImVec2(available.x, available.x / aspect);
    }
    imageSize = ImVec2(std::floor(std::max(1.0f, imageSize.x)), std::floor(std::max(1.0f, imageSize.y)));
    const ImVec2 imageMin(std::floor(areaMin.x + (available.x - imageSize.x) * 0.5f), std::floor(areaMin.y + (available.y - imageSize.y) * 0.5f));

    int renderWidth = preset.width;
    int renderHeight = preset.height;
    if (renderWidth == 0) {
        renderWidth = std::max(1, static_cast<int>(imageSize.x * io.DisplayFramebufferScale.x));
        renderHeight = std::max(1, static_cast<int>(imageSize.y * io.DisplayFramebufferScale.y));
    }
    context.renderGameView(renderWidth, renderHeight);

    ImGui::SetCursorScreenPos(imageMin);
    ImGui::Image(static_cast<ImTextureID>(context.renderer.getViewportTextureId(kGameViewTarget)), imageSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        ImGui::SetWindowFocus();
    }
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    context.gameViewFocused = focused;
    // В Play игра слушает клавиатуру, пока окно Game в фокусе, как в Unity.
    context.gameViewInputActive = context.isPlaying() && (focused || hovered) && !io.WantTextInput;

    const bool hasCamera = context.world.isAlive(context.gameCameraEntity) && context.world.hasComponent<Camera>(context.gameCameraEntity);
    if (!hasCamera) {
        EditorUI::pushSemibold();
        EditorUI::drawTextCentered(drawList, ImVec2(imageMin.x + imageSize.x * 0.5f, imageMin.y + imageSize.y * 0.5f - 10.0f),
            "Display 1", kTextDim);
        EditorUI::popFont();
        EditorUI::drawTextCentered(drawList, ImVec2(imageMin.x + imageSize.x * 0.5f, imageMin.y + imageSize.y * 0.5f + 10.0f),
            "No cameras rendering", kTextFaint);
    }

    // Подпись с разрешением в углу.
    char resolutionText[48];
    std::snprintf(resolutionText, sizeof(resolutionText), "%d \xC3\x97 %d", renderWidth, renderHeight);
    EditorUI::pushSmallFont();
    const ImVec2 labelSize = ImGui::CalcTextSize(resolutionText);
    const ImVec2 labelMin(imageMin.x + 8.0f, imageMin.y + 8.0f);
    drawList->AddRectFilled(labelMin, ImVec2(labelMin.x + labelSize.x + 14.0f, labelMin.y + labelSize.y + 6.0f),
        ImGui::GetColorU32(IM_COL32(0, 0, 0, 150)), 4.0f);
    drawList->AddText(ImVec2(labelMin.x + 7.0f, labelMin.y + 3.0f), ImGui::GetColorU32(IM_COL32(230, 230, 230, 255)), resolutionText);
    EditorUI::popFont();

    // HUD Lua-игры: статус, который пишет скрипт через world:set_status.
    const std::string& gameStatus = context.scripts.status();
    if (context.isPlaying() && !gameStatus.empty()) {
        EditorUI::pushSemibold();
        const bool defeat = gameStatus.rfind("DEFEAT", 0) == 0;
        const std::string text = EditorUI::ellipsize(gameStatus.c_str(), imageSize.x - 60.0f);
        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        const ImVec2 hudMin(std::floor(imageMin.x + (imageSize.x - textSize.x) * 0.5f - 14.0f), imageMin.y + 10.0f);
        const ImVec2 hudMax(hudMin.x + textSize.x + 28.0f, hudMin.y + textSize.y + 12.0f);
        drawList->AddRectFilled(hudMin, hudMax, ImGui::GetColorU32(defeat ? IM_COL32(120, 24, 24, 215) : IM_COL32(0, 0, 0, 165)), 14.0f);
        drawList->AddText(ImVec2(hudMin.x + 14.0f, hudMin.y + 6.0f), ImGui::GetColorU32(IM_COL32(240, 240, 244, 255)), text.c_str());
        EditorUI::popFont();
    }
    EditorUI::pushSmallFont();

    if (showStats) {
        char lines[3][64];
        std::snprintf(lines[0], sizeof(lines[0]), "%.0f FPS  (%.2f ms)", context.fpsAverage, context.frameTimeAverageMs);
        std::snprintf(lines[1], sizeof(lines[1]), "Collisions  %zu", context.physicsSystem.getLastCollisionCount());
        std::snprintf(lines[2], sizeof(lines[2]), "Animated  %zu", context.animationSystem.characterCount);
        float width = ImGui::CalcTextSize("0000 FPS  (000.00 ms)").x;
        for (const auto& line : lines) {
            width = std::max(width, ImGui::CalcTextSize(line).x);
        }
        const float lineHeight = ImGui::GetTextLineHeight() + 2.0f;
        const ImVec2 boxMin(imageMin.x + imageSize.x - width - 28.0f, imageMin.y + 8.0f);
        drawList->AddRectFilled(boxMin, ImVec2(boxMin.x + width + 20.0f, boxMin.y + lineHeight * 3.0f + 10.0f),
            ImGui::GetColorU32(IM_COL32(0, 0, 0, 160)), 6.0f);
        for (int i = 0; i < 3; ++i) {
            drawList->AddText(ImVec2(boxMin.x + 10.0f, boxMin.y + 5.0f + lineHeight * i), ImGui::GetColorU32(IM_COL32(235, 235, 235, 255)), lines[i]);
        }
    }

    if (!context.isPlaying()) {
        const char* hint = "Edit mode preview  \xC2\xB7  press Play to run the game";
        const ImVec2 hintSize = ImGui::CalcTextSize(hint);
        const ImVec2 hintMin(imageMin.x + (imageSize.x - hintSize.x) * 0.5f - 10.0f, imageMin.y + imageSize.y - hintSize.y - 18.0f);
        if (hintMin.y > imageMin.y + 40.0f) {
            drawList->AddRectFilled(hintMin, ImVec2(hintMin.x + hintSize.x + 20.0f, hintMin.y + hintSize.y + 8.0f),
                ImGui::GetColorU32(IM_COL32(0, 0, 0, 140)), 12.0f);
            drawList->AddText(ImVec2(hintMin.x + 10.0f, hintMin.y + 4.0f), ImGui::GetColorU32(IM_COL32(220, 220, 224, 255)), hint);
        }
    } else if (!context.gameViewInputActive) {
        const char* hint = "Click to give the game keyboard focus";
        const ImVec2 hintSize = ImGui::CalcTextSize(hint);
        const ImVec2 hintMin(imageMin.x + (imageSize.x - hintSize.x) * 0.5f - 10.0f, imageMin.y + imageSize.y - hintSize.y - 18.0f);
        drawList->AddRectFilled(hintMin, ImVec2(hintMin.x + hintSize.x + 20.0f, hintMin.y + hintSize.y + 8.0f),
            ImGui::GetColorU32(withAlpha(kAccent, 0.85f)), 12.0f);
        drawList->AddText(ImVec2(hintMin.x + 10.0f, hintMin.y + 4.0f), ImGui::GetColorU32(IM_COL32_WHITE), hint);
    }
    EditorUI::popFont();
    ImGui::End();
}
