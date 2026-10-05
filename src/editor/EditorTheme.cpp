#include "editor/EditorTheme.h"

#include "core/Logger.h"
#include "editor/IconsLucide.h"

#include <algorithm>
#include <filesystem>

namespace EditorTheme {
namespace {
Fonts gFonts;
float gUiScale = 1.0f;

constexpr const char* kInterRegular = "assets/fonts/Inter-Regular.ttf";
constexpr const char* kInterSemiBold = "assets/fonts/Inter-SemiBold.ttf";
constexpr const char* kJetBrainsMono = "assets/fonts/JetBrainsMono-Regular.ttf";
constexpr const char* kLucide = "assets/fonts/Lucide.ttf";

void mergeIcons(ImGuiIO& io, float size) {
    if (!std::filesystem::exists(kLucide)) {
        return;
    }
    static const ImWchar iconRanges[] = {ICON_MIN_LC, ICON_MAX_LC, 0};
    ImFontConfig config;
    config.MergeMode = true;
    config.PixelSnapH = true;
    // Иконки Lucide нарисованы в квадрате 24×24: выравниваем по центру строчных букв Inter.
    config.GlyphOffset = ImVec2(0.0f, 2.0f);
    config.GlyphMinAdvanceX = size;
    config.GlyphRanges = iconRanges;
    io.Fonts->AddFontFromFileTTF(kLucide, size, &config);
}

ImFont* addFont(ImGuiIO& io, const char* path, float size, bool withIcons) {
    if (!std::filesystem::exists(path)) {
        LOG_WARN(std::string("EditorTheme: font not found: ") + path);
        return nullptr;
    }
    // В Inter есть свои глифы в Private Use Area — те же коды, что у Lucide. Отдаём этот диапазон иконкам.
    static const ImWchar iconRange[] = {ICON_MIN_LC, ICON_MAX_LC, 0};
    ImFontConfig config;
    config.OversampleH = 2;
    if (withIcons) {
        config.GlyphExcludeRanges = iconRange;
    }
    ImFont* font = io.Fonts->AddFontFromFileTTF(path, size, &config);
    if (font && withIcons) {
        mergeIcons(io, size);
    }
    return font;
}
}

void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    gFonts.regular = addFont(io, kInterRegular, kFontSize, true);
    gFonts.semibold = addFont(io, kInterSemiBold, kFontSize, true);
    gFonts.mono = addFont(io, kJetBrainsMono, kMonoFontSize, false);
    if (gFonts.regular == nullptr) {
        gFonts.regular = io.Fonts->AddFontDefault();
    }
    if (gFonts.semibold == nullptr) {
        gFonts.semibold = gFonts.regular;
    }
    if (gFonts.mono == nullptr) {
        gFonts.mono = gFonts.regular;
    }
    io.FontDefault = gFonts.regular;
}

const Fonts& fonts() {
    return gFonts;
}

float uiScale() {
    return gUiScale;
}

ImVec4 toVec4(ImU32 color) {
    return ImGui::ColorConvertU32ToFloat4(color);
}

ImU32 withAlpha(ImU32 color, float alpha) {
    const ImU32 a = static_cast<ImU32>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

ImU32 mix(ImU32 a, ImU32 b, float t) {
    const ImVec4 x = toVec4(a);
    const ImVec4 y = toVec4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(
        x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
}

void apply(float scale) {
    gUiScale = std::clamp(scale, 0.75f, 2.0f);
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();

    style.FontSizeBase = kFontSize;
    style.FontScaleMain = gUiScale;

    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.42f;
    style.WindowPadding = ImVec2(10.0f, 8.0f);
    style.WindowRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.WindowMinSize = ImVec2(64.0f, 48.0f);
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.ChildRounding = 4.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupRounding = 6.0f;
    style.PopupBorderSize = 1.0f;
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.FrameRounding = 4.0f;
    style.FrameBorderSize = 0.0f;
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.CellPadding = ImVec2(6.0f, 3.0f);
    style.IndentSpacing = 14.0f;
    style.ColumnsMinSpacing = 6.0f;
    style.ScrollbarSize = 11.0f;
    style.ScrollbarRounding = 8.0f;
    style.ScrollbarPadding = 2.0f;
    style.GrabMinSize = 10.0f;
    style.GrabRounding = 3.0f;
    style.ImageBorderSize = 0.0f;
    style.TabRounding = 5.0f;
    style.TabBorderSize = 0.0f;
    style.TabMinWidthBase = 1.0f;
    style.TabCloseButtonMinWidthSelected = -1.0f;
    style.TabCloseButtonMinWidthUnselected = 0.0f;
    style.TabBarBorderSize = 1.0f;
    style.TabBarOverlineSize = 2.0f;
    style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesNone;
    style.TreeLinesSize = 1.0f;
    style.TreeLinesRounding = 3.0f;
    style.DragDropTargetRounding = 4.0f;
    style.DragDropTargetBorderSize = 2.0f;
    style.DragDropTargetPadding = 2.0f;
    style.ColorMarkerSize = 2.0f;
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorSize = 1.0f;
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextPadding = ImVec2(0.0f, 4.0f);
    style.DockingNodeHasCloseButton = false;
    style.DockingSeparatorSize = 3.0f;
    style.HoverDelayShort = 0.2f;
    style.HoverDelayNormal = 0.45f;
    style.HoverStationaryDelay = 0.2f;
    style.CircleTessellationMaxError = 0.2f;

    ImVec4* colors = style.Colors;
    auto set = [&](ImGuiCol index, ImU32 color) { colors[index] = toVec4(color); };
    set(ImGuiCol_Text, kText);
    set(ImGuiCol_TextDisabled, kTextFaint);
    set(ImGuiCol_WindowBg, kPanel);
    set(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    set(ImGuiCol_PopupBg, IM_COL32(36, 36, 38, 252));
    set(ImGuiCol_Border, kBorder);
    set(ImGuiCol_BorderShadow, IM_COL32(0, 0, 0, 0));
    set(ImGuiCol_FrameBg, kFrame);
    set(ImGuiCol_FrameBgHovered, kFrameHovered);
    set(ImGuiCol_FrameBgActive, kFrameActive);
    set(ImGuiCol_TitleBg, kBackground);
    set(ImGuiCol_TitleBgActive, kBackground);
    set(ImGuiCol_TitleBgCollapsed, kBackground);
    set(ImGuiCol_MenuBarBg, kBackground);
    set(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
    set(ImGuiCol_ScrollbarGrab, IM_COL32(70, 70, 74, 255));
    set(ImGuiCol_ScrollbarGrabHovered, IM_COL32(88, 88, 94, 255));
    set(ImGuiCol_ScrollbarGrabActive, IM_COL32(104, 104, 110, 255));
    set(ImGuiCol_CheckMark, IM_COL32(255, 255, 255, 255));
    set(ImGuiCol_SliderGrab, kAccent);
    set(ImGuiCol_SliderGrabActive, kAccentHovered);
    set(ImGuiCol_Button, IM_COL32(48, 48, 51, 255));
    set(ImGuiCol_ButtonHovered, IM_COL32(60, 60, 64, 255));
    set(ImGuiCol_ButtonActive, IM_COL32(72, 72, 77, 255));
    set(ImGuiCol_Header, kAccentSoft);
    set(ImGuiCol_HeaderHovered, IM_COL32(255, 255, 255, 14));
    set(ImGuiCol_HeaderActive, IM_COL32(61, 139, 253, 80));
    set(ImGuiCol_Separator, IM_COL32(54, 54, 58, 255));
    set(ImGuiCol_SeparatorHovered, kAccent);
    set(ImGuiCol_SeparatorActive, kAccentHovered);
    set(ImGuiCol_ResizeGrip, IM_COL32(0, 0, 0, 0));
    set(ImGuiCol_ResizeGripHovered, kAccentSoft);
    set(ImGuiCol_ResizeGripActive, kAccent);
    set(ImGuiCol_InputTextCursor, kAccentHovered);
    set(ImGuiCol_Tab, kBackground);
    set(ImGuiCol_TabHovered, IM_COL32(40, 40, 42, 255));
    set(ImGuiCol_TabSelected, kPanel);
    set(ImGuiCol_TabSelectedOverline, kAccent);
    set(ImGuiCol_TabDimmed, kBackground);
    set(ImGuiCol_TabDimmedSelected, kPanel);
    set(ImGuiCol_TabDimmedSelectedOverline, IM_COL32(0, 0, 0, 0));
    set(ImGuiCol_DockingPreview, IM_COL32(61, 139, 253, 90));
    set(ImGuiCol_DockingEmptyBg, kBackground);
    set(ImGuiCol_PlotLines, kAccent);
    set(ImGuiCol_PlotLinesHovered, kAccentHovered);
    set(ImGuiCol_PlotHistogram, kAccent);
    set(ImGuiCol_PlotHistogramHovered, kAccentHovered);
    set(ImGuiCol_TableHeaderBg, kPanelRaised);
    set(ImGuiCol_TableBorderStrong, kBorder);
    set(ImGuiCol_TableBorderLight, IM_COL32(40, 40, 43, 255));
    set(ImGuiCol_TableRowBg, IM_COL32(0, 0, 0, 0));
    set(ImGuiCol_TableRowBgAlt, IM_COL32(255, 255, 255, 6));
    set(ImGuiCol_TextLink, kAccentHovered);
    set(ImGuiCol_TextSelectedBg, IM_COL32(61, 139, 253, 90));
    set(ImGuiCol_TreeLines, IM_COL32(70, 70, 76, 255));
    set(ImGuiCol_DragDropTarget, kAccent);
    set(ImGuiCol_DragDropTargetBg, IM_COL32(61, 139, 253, 30));
    set(ImGuiCol_UnsavedMarker, kText);
    set(ImGuiCol_NavCursor, IM_COL32(61, 139, 253, 110));
    set(ImGuiCol_NavWindowingHighlight, IM_COL32(255, 255, 255, 180));
    set(ImGuiCol_NavWindowingDimBg, IM_COL32(0, 0, 0, 100));
    set(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 140));

    if (gUiScale != 1.0f) {
        style.ScaleAllSizes(gUiScale);
    }
}
} // namespace EditorTheme
