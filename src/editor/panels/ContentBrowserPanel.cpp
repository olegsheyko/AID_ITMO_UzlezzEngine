#include "editor/panels/ContentBrowserPanel.h"

#include "editor/EditorContext.h"
#include "editor/EditorIcons.h"
#include "editor/EditorTheme.h"
#include "editor/EditorWidgets.h"
#include "editor/EditorWindows.h"
#include "editor/IconsLucide.h"
#include "editor/PlatformShell.h"
#include "editor/ThumbnailCache.h"
#include "resources/ResourceManager.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>

using namespace EditorTheme;

namespace {
constexpr float kRefreshInterval = 2.0f;
constexpr float kTileSpacing = 12.0f;

ImU32 styled(ImU32 color) {
    return ImGui::GetColorU32(color);
}

std::string displayName(const AssetEntry& entry) {
    if (entry.type == AssetType::Folder || entry.extension.empty()) {
        return entry.name;
    }
    return entry.name.substr(0, entry.name.size() - entry.extension.size());
}

std::string folderLabel(const std::string& path, const std::string& root) {
    if (path == root) {
        return "Assets";
    }
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string formatTime(std::filesystem::file_time_type time) {
    using namespace std::chrono;
    const auto system = time_point_cast<system_clock::duration>(time - std::filesystem::file_time_type::clock::now() + system_clock::now());
    const std::time_t t = system_clock::to_time_t(system);
    char buffer[32] = {};
    if (const std::tm* local = std::localtime(&t)) {
        std::strftime(buffer, sizeof(buffer), "%d.%m.%Y %H:%M", local);
    }
    return buffer;
}

void drawTextCenteredLarge(ImDrawList* drawList, const ImVec2& center, const char* text, float fontSize, ImU32 color) {
    ImGui::PushFont(nullptr, fontSize);
    EditorUI::drawTextCentered(drawList, center, text, color);
    ImGui::PopFont();
}

void drawFolderIcon(ImDrawList* drawList, const ImVec2& min, float size, bool hovered) {
    const float width = size * 0.84f;
    const float height = size * 0.66f;
    const ImVec2 origin(min.x + (size - width) * 0.5f, min.y + (size - height) * 0.5f + size * 0.02f);
    const float rounding = std::max(2.0f, size * 0.07f);
    const ImU32 back = styled(hovered ? IM_COL32(222, 164, 58, 255) : IM_COL32(206, 148, 48, 255));
    const ImU32 front = styled(hovered ? IM_COL32(255, 204, 98, 255) : IM_COL32(242, 189, 78, 255));
    drawList->AddRectFilled(origin, ImVec2(origin.x + width * 0.44f, origin.y + height * 0.3f), back, rounding);
    drawList->AddRectFilled(ImVec2(origin.x, origin.y + height * 0.12f), ImVec2(origin.x + width, origin.y + height), back, rounding);
    drawList->AddRectFilled(ImVec2(origin.x, origin.y + height * 0.27f), ImVec2(origin.x + width, origin.y + height), front, rounding);
    drawList->AddLine(ImVec2(origin.x + rounding, origin.y + height * 0.27f + 1.0f),
        ImVec2(origin.x + width - rounding, origin.y + height * 0.27f + 1.0f), styled(IM_COL32(255, 228, 160, 110)), 1.0f);
}

void drawDocumentIcon(ImDrawList* drawList, const ImVec2& min, float size, ImU32 accent, const char* badgeText, const char* glyph, bool hovered) {
    const float width = size * 0.62f;
    const float height = size * 0.8f;
    const ImVec2 o(min.x + (size - width) * 0.5f, min.y + (size - height) * 0.5f);
    const float fold = width * 0.3f;
    const float r = std::max(2.0f, size * 0.05f);
    const ImU32 paper = styled(hovered ? IM_COL32(236, 238, 243, 255) : IM_COL32(220, 222, 229, 255));
    const ImU32 shade = styled(IM_COL32(166, 170, 182, 255));

    drawList->PathArcToFast(ImVec2(o.x + r, o.y + r), r, 6, 9);
    drawList->PathLineTo(ImVec2(o.x + width - fold, o.y));
    drawList->PathLineTo(ImVec2(o.x + width, o.y + fold));
    drawList->PathArcToFast(ImVec2(o.x + width - r, o.y + height - r), r, 0, 3);
    drawList->PathArcToFast(ImVec2(o.x + r, o.y + height - r), r, 3, 6);
    drawList->PathFillConvex(paper);
    drawList->AddTriangleFilled(ImVec2(o.x + width - fold, o.y), ImVec2(o.x + width - fold, o.y + fold), ImVec2(o.x + width, o.y + fold), shade);

    if (glyph) {
        ImGui::PushFont(nullptr, std::max(10.0f, size * 0.26f));
        EditorUI::drawTextCentered(drawList, ImVec2(o.x + width * 0.5f, o.y + height * 0.38f), glyph, withAlpha(accent, 0.85f));
        ImGui::PopFont();
    }
    if (badgeText && badgeText[0]) {
        const float fontSize = std::clamp(size * 0.14f, 8.0f, 13.0f);
        ImGui::PushFont(fonts().semibold, fontSize);
        const ImVec2 textSize = ImGui::CalcTextSize(badgeText);
        const float badgeWidth = std::max(width * 0.78f, textSize.x + 8.0f);
        const float badgeHeight = textSize.y + std::max(2.0f, size * 0.03f);
        const ImVec2 badgeMin(o.x + (width - badgeWidth) * 0.5f, o.y + height * 0.66f);
        const ImVec2 badgeMax(badgeMin.x + badgeWidth, badgeMin.y + badgeHeight);
        drawList->AddRectFilled(badgeMin, badgeMax, styled(accent), std::max(2.0f, size * 0.04f));
        drawList->AddText(ImVec2(std::floor(badgeMin.x + (badgeWidth - textSize.x) * 0.5f), std::floor(badgeMin.y + (badgeHeight - textSize.y) * 0.5f)),
            styled(IM_COL32(255, 255, 255, 255)), badgeText);
        ImGui::PopFont();
    }
}

void drawChecker(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float cell) {
    drawList->AddRectFilled(min, max, styled(IM_COL32(48, 48, 52, 255)), 4.0f);
    drawList->PushClipRect(min, max, true);
    for (float y = min.y; y < max.y; y += cell) {
        for (float x = min.x; x < max.x; x += cell) {
            if ((static_cast<int>((x - min.x) / cell) + static_cast<int>((y - min.y) / cell)) % 2 == 0) {
                drawList->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + cell, max.x), std::min(y + cell, max.y)), styled(IM_COL32(58, 58, 62, 255)));
            }
        }
    }
    drawList->PopClipRect();
}

// Подпись типа под именем, как в карточках UE5.
const char* cardTypeName(AssetType type) {
    switch (type) {
    case AssetType::Model: return "Mesh";
    case AssetType::Material: return "Material";
    case AssetType::Texture: return "Texture";
    case AssetType::Shader: return "Shader";
    case AssetType::Scene: return "Scene";
    case AssetType::Json: return "Data";
    case AssetType::Text: return "Text";
    case AssetType::Font: return "Font";
    case AssetType::Audio: return "Sound";
    case AssetType::Script: return "Script";
    case AssetType::Folder: return "Folder";
    case AssetType::Other: return "File";
    }
    return "File";
}

bool isBreakChar(char c) {
    return c == ' ' || c == '_' || c == '-' || c == '.';
}

// Имя в две строки: перенос по пробелу или «_», вторая строка — с многоточием.
std::pair<std::string, std::string> wrapTwoLines(const std::string& text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width) {
        return {text, std::string()};
    }
    std::size_t fit = 0;
    std::size_t lastBreak = 0;
    for (std::size_t i = 1; i <= text.size(); ++i) {
        if (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) {
            continue;
        }
        if (ImGui::CalcTextSize(text.c_str(), text.c_str() + i).x > width) {
            break;
        }
        fit = i;
        if (isBreakChar(text[i - 1])) {
            lastBreak = i;
        }
    }
    const std::size_t cut = (lastBreak > fit / 2) ? lastBreak : std::max<std::size_t>(fit, 1);
    std::string first = text.substr(0, cut);
    std::string rest = text.substr(cut);
    while (!first.empty() && first.back() == ' ') {
        first.pop_back();
    }
    while (!rest.empty() && rest.front() == ' ') {
        rest.erase(rest.begin());
    }
    return {first, EditorUI::ellipsize(rest.c_str(), width)};
}

const char* glyphFor(AssetType type) {
    switch (type) {
    case AssetType::Model: return ICON_LC_BOX;
    case AssetType::Shader: return ICON_LC_BRACES;
    case AssetType::Scene: return ICON_LC_CLAPPERBOARD;
    case AssetType::Material: return ICON_LC_PALETTE;
    case AssetType::Json: return ICON_LC_BRACES;
    case AssetType::Text: return ICON_LC_FILE_TEXT;
    case AssetType::Font: return ICON_LC_TYPE;
    case AssetType::Audio: return ICON_LC_AUDIO_LINES;
    case AssetType::Script: return ICON_LC_FILE_CODE;
    default: return nullptr;
    }
}
}

void ContentBrowserPanel::navigate(EditorContext& context, const std::string& folder, bool recordHistory) {
    if (!context.assets.exists(folder) || folder == currentFolder_) {
        return;
    }
    currentFolder_ = folder;
    revealFolder_ = true;
    if (recordHistory) {
        if (historyIndex_ + 1 < static_cast<int>(history_.size())) {
            history_.resize(static_cast<std::size_t>(historyIndex_ + 1));
        }
        history_.push_back(folder);
        historyIndex_ = static_cast<int>(history_.size()) - 1;
    }
}

void ContentBrowserPanel::openFolder(EditorContext& context, const std::string& folder) {
    context.assets.refresh();
    if (!initialized_) {
        navigate(context, context.assets.root());
        initialized_ = true;
    }
    search_[0] = '\0';
    navigate(context, folder);
}

void ContentBrowserPanel::draw(EditorContext& context, float dt) {
    if (!initialized_) {
        context.assets.refresh();
        navigate(context, context.assets.root());
        initialized_ = true;
    }
    refreshTimer_ += dt;
    if (refreshTimer_ >= kRefreshInterval) {
        refreshTimer_ = 0.0f;
        if (context.assets.refresh()) {
            ThumbnailCache::instance().forgetFailures();
        }
        if (!context.assets.exists(currentFolder_)) {
            currentFolder_.clear();
            navigate(context, context.assets.root());
        }
    }
    if (!context.revealAssetRequest.empty()) {
        const std::string path = context.revealAssetRequest;
        context.revealAssetRequest.clear();
        context.assets.refresh();
        search_[0] = '\0';
        navigate(context, AssetDatabase::parentFolder(path));
        context.selectAsset(path);
        scrollToPath_ = path;
        ImGui::SetWindowFocus(EditorWindow::kContentBrowser);
    }

    if (!open) {
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin(EditorWindow::kContentBrowser, &open);
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return;
    }

    drawToolbar(context);

    const bool searching = search_[0] != '\0';
    const std::vector<AssetEntry> entries = searching ? context.assets.search(search_.data()) : context.assets.list(currentFolder_);
    const float footerHeight = ImGui::GetTextLineHeight() + 8.0f;
    const float bodyHeight = std::max(10.0f, ImGui::GetContentRegionAvail().y - footerHeight);

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginTable("##browser_split", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoSavedSettings,
            ImVec2(0.0f, bodyHeight))) {
        ImGui::TableSetupColumn("tree", ImGuiTableColumnFlags_WidthFixed, treeWidth_);
        ImGui::TableSetupColumn("items", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow(ImGuiTableRowFlags_None, bodyHeight);

        ImGui::TableSetColumnIndex(0);
        treeWidth_ = std::max(120.0f, ImGui::GetContentRegionAvail().x);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
        ImGui::BeginChild("##folder_tree", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
        drawFolderTree(context, context.assets.root(), 0);
        revealFolder_ = false;
        ImGui::PopStyleVar(2);
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::TableSetColumnIndex(1);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
        ImGui::BeginChild("##items", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
        if (entries.empty()) {
            if (searching) {
                EditorUI::emptyState(ICON_LC_SEARCH, "No assets found", "Nothing in the project matches this name");
            } else {
                EditorUI::emptyState(ICON_LC_FOLDER_OPEN, "This folder is empty", "Drop files into it from Finder or Explorer");
            }
        } else if (listView) {
            drawList(context, entries);
        } else {
            drawGrid(context, entries);
        }
        // Ctrl/Cmd + колесо — размер плиток, как в Unity.
        if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f) {
            tileSize = std::clamp(tileSize + ImGui::GetIO().MouseWheel * 8.0f, 72.0f, 168.0f);
        }
        if (ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() && !searching &&
            ImGui::IsKeyPressed(ImGuiKey_Backspace) && currentFolder_ != context.assets.root()) {
            navigate(context, AssetDatabase::parentFolder(currentFolder_));
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    drawFooter(context, entries.size());
    ImGui::End();
}

void ContentBrowserPanel::drawToolbar(EditorContext& context) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetFrameHeight() + 10.0f;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddLine(ImVec2(min.x, min.y + height - 1.0f), ImVec2(min.x + width, min.y + height - 1.0f), styled(kBorder));

    ImGui::SetCursorScreenPos(ImVec2(min.x + 8.0f, min.y + 5.0f));
    ImGui::BeginDisabled(historyIndex_ <= 0);
    if (EditorUI::iconButton("##back", ICON_LC_ARROW_LEFT, "Back")) {
        --historyIndex_;
        navigate(context, history_[static_cast<std::size_t>(historyIndex_)], false);
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::BeginDisabled(historyIndex_ + 1 >= static_cast<int>(history_.size()));
    if (EditorUI::iconButton("##forward", ICON_LC_ARROW_RIGHT, "Forward")) {
        ++historyIndex_;
        navigate(context, history_[static_cast<std::size_t>(historyIndex_)], false);
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::BeginDisabled(currentFolder_ == context.assets.root());
    if (EditorUI::iconButton("##up", ICON_LC_ARROW_UP, "Up one level (Backspace)")) {
        navigate(context, AssetDatabase::parentFolder(currentFolder_));
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 10.0f);

    // Правая часть: поиск, вид, размер, обновить.
    const float searchWidth = std::clamp(width * 0.24f, 140.0f, 260.0f);
    const float sliderWidth = 90.0f;
    const float rightWidth = searchWidth + sliderWidth + ImGui::GetFrameHeight() * 3.0f + style.ItemSpacing.x * 4.0f + 16.0f;
    const float breadcrumbsWidth = std::max(60.0f, width - (ImGui::GetCursorScreenPos().x - min.x) - rightWidth);
    drawBreadcrumbs(context, breadcrumbsWidth);

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorScreenPos(ImVec2(min.x + width - rightWidth + 8.0f, min.y + 5.0f));
    EditorUI::searchBox("asset_search", search_.data(), search_.size(), "Search assets", searchWidth);
    ImGui::SameLine();
    if (EditorUI::iconButton("##grid_view", ICON_LC_LAYOUT_GRID, "Grid view", !listView)) {
        listView = false;
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (EditorUI::iconButton("##list_view", ICON_LC_LIST, "List view", listView)) {
        listView = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(listView);
    ImGui::SetNextItemWidth(sliderWidth);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 3.0f));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
    ImGui::SliderFloat("##tile_size", &tileSize, 72.0f, 168.0f, "");
    ImGui::PopStyleVar();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::SetTooltip("Tile size (%s + wheel)", EditorUI::shortcut("Ctrl+").c_str());
    }
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, min.y + 5.0f));
    if (EditorUI::iconButton("##refresh", ICON_LC_REFRESH_CW, "Refresh")) {
        context.assets.refresh();
        ThumbnailCache::instance().forgetFailures();
    }
    ImGui::SetCursorScreenPos(ImVec2(min.x, min.y + height));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void ContentBrowserPanel::drawBreadcrumbs(EditorContext& context, float maxWidth) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float startX = ImGui::GetCursorScreenPos().x;
    const float height = ImGui::GetFrameHeight();
    if (search_[0] != '\0') {
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const std::string label = std::string(ICON_LC_SEARCH "  Results for \"") + search_.data() + "\"";
        EditorUI::drawTextEllipsis(drawList, ImVec2(position.x + 4.0f, position.y + (height - ImGui::GetFontSize()) * 0.5f), maxWidth, label.c_str(), kTextDim);
        ImGui::Dummy(ImVec2(maxWidth, height));
        return;
    }

    std::vector<std::string> segments;
    for (std::string folder = currentFolder_; !folder.empty(); folder = AssetDatabase::parentFolder(folder)) {
        segments.push_back(folder);
        if (folder == context.assets.root()) {
            break;
        }
    }
    std::reverse(segments.begin(), segments.end());
    // Не влезает — прячем начало пути за «…».
    std::size_t first = 0;
    auto widthFrom = [&](std::size_t index) {
        float total = 0.0f;
        for (std::size_t i = index; i < segments.size(); ++i) {
            total += ImGui::CalcTextSize(folderLabel(segments[i], context.assets.root()).c_str()).x + 30.0f;
        }
        return total;
    };
    while (first + 1 < segments.size() && widthFrom(first) > maxWidth) {
        ++first;
    }

    for (std::size_t i = first; i < segments.size(); ++i) {
        if (i > first || first > 0) {
            if (i == first && first > 0) {
                ImGui::AlignTextToFramePadding();
                EditorUI::textFaint("...");
                ImGui::SameLine(0.0f, 4.0f);
            }
            if (i > first) {
                ImGui::AlignTextToFramePadding();
                EditorUI::textFaint(ICON_LC_CHEVRON_RIGHT);
                ImGui::SameLine(0.0f, 2.0f);
            }
        }
        const std::string label = folderLabel(segments[i], context.assets.root());
        const bool last = i + 1 == segments.size();
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
        const ImVec2 position = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##crumb", ImVec2(textSize.x + 10.0f, height))) {
            navigate(context, segments[i]);
        }
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) {
            drawList->AddRectFilled(position, ImVec2(position.x + textSize.x + 10.0f, position.y + height), styled(kFrameHovered), 4.0f);
        }
        if (last) {
            ImGui::PushFont(fonts().semibold, 0.0f);
        }
        drawList->AddText(ImVec2(position.x + 5.0f, position.y + (height - ImGui::GetFontSize()) * 0.5f),
            styled(last ? kText : (hovered ? kText : kTextDim)), label.c_str());
        if (last) {
            ImGui::PopFont();
        }
        ImGui::PopID();
        if (!last) {
            ImGui::SameLine(0.0f, 2.0f);
        }
    }
    const float used = ImGui::GetItemRectMax().x - startX;
    if (used < maxWidth) {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::Dummy(ImVec2(maxWidth - used, height));
    }
}

void ContentBrowserPanel::drawFolderTree(EditorContext& context, const std::string& folder, int depth) {
    if (depth > 32) {
        return;
    }
    const bool isRoot = folder == context.assets.root();
    const bool hasChildren = context.assets.hasSubfolders(folder);
    const bool current = folder == currentFolder_ && search_[0] == '\0';
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding;
    if (!hasChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (current) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (isRoot) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }
    // После перехода раскрываем путь к текущей папке; дальше пользователь сворачивает что хочет.
    if (revealFolder_ && hasChildren && !current && currentFolder_.rfind(folder + "/", 0) == 0) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }
    ImGui::PushID(folder.c_str());
    const float labelX = ImGui::GetCursorScreenPos().x + ImGui::GetTreeNodeToLabelSpacing();
    const bool nodeOpen = ImGui::TreeNodeEx("##folder", flags);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
        search_[0] = '\0';
        navigate(context, folder);
    }
    if (ImGui::BeginPopupContextItem("##folder_context")) {
        const std::string reveal = std::string(ICON_LC_FOLDER_OPEN "  Show in ") + PlatformShell::fileManagerName();
        if (ImGui::MenuItem(reveal.c_str())) {
            PlatformShell::revealInFileManager(folder);
        }
        if (ImGui::MenuItem(ICON_LC_COPY "  Copy Path")) {
            ImGui::SetClipboardText(folder.c_str());
        }
        ImGui::EndPopup();
    }
    const ImVec2 rowMin = ImGui::GetItemRectMin();
    const ImVec2 rowMax = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float textY = rowMin.y + (rowMax.y - rowMin.y - ImGui::GetFontSize()) * 0.5f;
    drawList->AddText(ImVec2(labelX, textY), styled(isRoot ? kAccentHovered : kFolder),
        isRoot ? ICON_LC_HARD_DRIVE : (nodeOpen && hasChildren ? ICON_LC_FOLDER_OPEN : ICON_LC_FOLDER));
    const std::string label = folderLabel(folder, context.assets.root());
    const float iconAdvance = ImGui::GetFontSize() + EditorUI::px(7.0f);
    EditorUI::drawTextEllipsis(drawList, ImVec2(labelX + iconAdvance, textY), rowMax.x - labelX - iconAdvance - 4.0f, label.c_str(),
        current ? kText : IM_COL32(200, 200, 204, 255));
    if (nodeOpen) {
        for (const AssetEntry& child : context.assets.subfolders(folder)) {
            drawFolderTree(context, child.path, depth + 1);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

float ContentBrowserPanel::cardHeight(float size) const {
    const float lineHeight = ImGui::GetTextLineHeight();
    return size + EditorUI::px(3.0f) + EditorUI::px(6.0f) + lineHeight * 2.0f + EditorTheme::kSmallFontSize * EditorTheme::uiScale() +
        EditorUI::px(10.0f);
}

const std::pair<std::string, std::string>& ContentBrowserPanel::wrappedName(const AssetEntry& entry, float width) {
    const std::string key = entry.path + "|" + std::to_string(static_cast<int>(width)) + "|" + std::to_string(EditorTheme::uiScale());
    auto it = nameCache_.find(key);
    if (it == nameCache_.end()) {
        if (nameCache_.size() > 4096) {
            nameCache_.clear();
        }
        it = nameCache_.emplace(key, wrapTwoLines(displayName(entry), width)).first;
    }
    return it->second;
}

void ContentBrowserPanel::drawTile(EditorContext& context, const AssetEntry& entry, const ImVec2& min, float size, bool selected, bool hovered) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float height = cardHeight(size);
    const ImVec2 max(min.x + size, min.y + height);
    const float rounding = EditorUI::px(6.0f);
    const ImU32 accent = assetColor(entry.type);

    if (entry.type == AssetType::Folder) {
        // Папки — без карточки, как в UE5: иконка и подпись, подсветка при наведении и выделении.
        if (selected || hovered) {
            drawList->AddRectFilled(min, max, styled(selected ? kAccentSoft : IM_COL32(255, 255, 255, 12)), rounding);
            if (selected) {
                drawList->AddRect(min, max, styled(withAlpha(kAccent, 0.6f)), rounding, 0, 1.5f);
            }
        }
        drawFolderIcon(drawList, min, size, hovered);
    } else {
        // Карточка: превью, цветная полоса типа, имя и тип.
        const ImU32 cardColor = selected ? IM_COL32(36, 52, 80, 255) : (hovered ? IM_COL32(48, 48, 52, 255) : IM_COL32(38, 38, 41, 255));
        drawList->AddRectFilled(min, max, styled(cardColor), rounding);
        const ImVec2 thumbMax(max.x, min.y + size);
        drawList->AddRectFilled(min, thumbMax, styled(hovered ? IM_COL32(58, 59, 64, 255) : IM_COL32(50, 51, 56, 255)), rounding,
            ImDrawFlags_RoundCornersTop);
        // Мягкий «софтбокс» сверху — фон студийного рендера.
        drawList->AddCircleFilled(ImVec2(min.x + size * 0.5f, min.y + size * 0.35f), size * 0.42f,
            styled(IM_COL32(255, 255, 255, hovered ? 12 : 8)), 48);

        bool drawn = false;
        if (entry.type == AssetType::Texture && AssetDatabase::isLoadableTexture(entry.extension)) {
            ThumbnailCache& thumbnails = ThumbnailCache::instance();
            const TextureData* texture = thumbnails.texture(entry.path);
            if (texture && texture->width > 0 && texture->height > 0) {
                const float inset = size * 0.1f;
                const float boxSize = size - inset * 2.0f;
                const float scale = boxSize / static_cast<float>(std::max(texture->width, texture->height));
                const ImVec2 dims(texture->width * scale, texture->height * scale);
                const ImVec2 start(min.x + inset + (boxSize - dims.x) * 0.5f, min.y + inset + (boxSize - dims.y) * 0.5f);
                const ImVec2 end(start.x + dims.x, start.y + dims.y);
                drawChecker(drawList, start, end, std::max(4.0f, size * 0.08f));
                drawList->AddImageRounded(static_cast<ImTextureID>(texture->textureId), start, end, ImVec2(0, 1), ImVec2(1, 0),
                    styled(IM_COL32_WHITE), 3.0f);
                drawList->AddRect(start, end, styled(IM_COL32(0, 0, 0, 80)), 3.0f);
                drawn = true;
            } else if (thumbnails.isLoading(entry.path)) {
                drawTextCenteredLarge(drawList, ImVec2(min.x + size * 0.5f, min.y + size * 0.5f), ICON_LC_LOADER_CIRCLE, size * 0.22f, kTextFaint);
                drawn = true;
            }
        } else if (AssetPreviewer::supports(entry.type)) {
            const AssetThumbnail thumbnail = context.previewer.thumbnail(entry.path);
            if (thumbnail.texture != 0) {
                drawList->AddImageRounded(static_cast<ImTextureID>(thumbnail.texture), min, thumbMax, ImVec2(0, 1), ImVec2(1, 0),
                    styled(IM_COL32_WHITE), rounding, ImDrawFlags_RoundCornersTop);
                drawn = true;
            } else if (thumbnail.loading) {
                drawTextCenteredLarge(drawList, ImVec2(min.x + size * 0.5f, min.y + size * 0.5f), ICON_LC_LOADER_CIRCLE, size * 0.22f, kTextFaint);
                drawn = true;
            }
        }
        if (!drawn) {
            const float iconSize = size * 0.82f;
            drawDocumentIcon(drawList, ImVec2(min.x + (size - iconSize) * 0.5f, min.y + (size - iconSize) * 0.5f), iconSize, accent,
                AssetDatabase::badgeText(entry).c_str(), glyphFor(entry.type), hovered);
        }
        drawList->AddRectFilled(ImVec2(min.x, thumbMax.y), ImVec2(max.x, thumbMax.y + EditorUI::px(3.0f)), styled(accent));
        if (selected) {
            drawList->AddRect(min, max, styled(withAlpha(kAccent, 0.85f)), rounding, 0, 1.5f);
        }
    }

    // Имя в две строки и тип.
    const float padding = EditorUI::px(6.0f);
    const float textWidth = size - padding * 2.0f;
    const auto& name = wrappedName(entry, textWidth);
    const float lineHeight = ImGui::GetTextLineHeight();
    float y = min.y + size + EditorUI::px(3.0f) + EditorUI::px(5.0f);
    const ImU32 nameColor = selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(222, 222, 226, 255);
    const bool centered = entry.type == AssetType::Folder;
    auto drawLine = [&](const std::string& line, ImU32 color) {
        const float width = ImGui::CalcTextSize(line.c_str()).x;
        const float x = centered ? std::floor(min.x + (size - width) * 0.5f) : min.x + padding;
        drawList->AddText(ImVec2(x, y), styled(color), line.c_str());
        y += lineHeight;
    };
    drawLine(name.first, nameColor);
    if (!name.second.empty()) {
        drawLine(name.second, nameColor);
    }
    if (entry.type != AssetType::Folder) {
        EditorUI::pushSmallFont();
        drawList->AddText(ImVec2(min.x + padding, min.y + height - padding - ImGui::GetFontSize()), styled(kTextFaint), cardTypeName(entry.type));
        EditorUI::popFont();
    }
}

void ContentBrowserPanel::handleItemInteraction(EditorContext& context, const AssetEntry& entry) {
    // Выделяем по отпусканию, а не по нажатию: иначе при перетаскивании ассета в слот инспектора
    // инспектор успевает переключиться на сам ассет и слот исчезает (в Unity так же).
    if (ImGui::IsItemDeactivated() && ImGui::IsItemHovered() && draggingPath_ != entry.path) {
        context.selectAsset(entry.path);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        context.selectAsset(entry.path);
    }
    if (ImGui::IsItemDeactivated() && draggingPath_ == entry.path) {
        draggingPath_.clear();
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        activate(context, entry);
    }
    if (entry.type != AssetType::Folder && ImGui::BeginDragDropSource()) {
        draggingPath_ = entry.path;
        ImGui::SetDragDropPayload(kAssetPayload, entry.path.c_str(), entry.path.size() + 1);
        ImU32 color = 0;
        const char* icon = assetIcon(entry.type, &color);
        EditorUI::iconLabel(icon, entry.name.c_str(), color);
        ImGui::EndDragDropSource();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay) && !ImGui::GetDragDropPayload()) {
        ImGui::BeginTooltip();
        EditorUI::pushSemibold();
        ImGui::TextUnformatted(entry.name.c_str());
        EditorUI::popFont();
        EditorUI::pushSmallFont();
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextDim));
        if (entry.type == AssetType::Folder) {
            ImGui::Text("Folder  \xC2\xB7  %d items", static_cast<int>(context.assets.list(entry.path).size()));
        } else {
            ImGui::Text("%s  \xC2\xB7  %s", AssetDatabase::typeName(entry.type), AssetDatabase::formatSize(entry.size).c_str());
            if (entry.type == AssetType::Texture && AssetDatabase::isLoadableTexture(entry.extension)) {
                if (const TextureData* texture = ThumbnailCache::instance().texture(entry.path)) {
                    ImGui::Text("%d \xC3\x97 %d", texture->width, texture->height);
                }
            }
        }
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextFaint));
        ImGui::TextUnformatted(entry.path.c_str());
        ImGui::PopStyleColor();
        EditorUI::popFont();
        ImGui::EndTooltip();
    }
    itemContextMenu(context, entry);
}

void ContentBrowserPanel::activate(EditorContext& context, const AssetEntry& entry) {
    switch (entry.type) {
    case AssetType::Folder:
        search_[0] = '\0';
        navigate(context, entry.path);
        break;
    case AssetType::Scene:
        context.loadScene(entry.path);
        break;
    case AssetType::Model:
        if (!context.isPlaying()) {
            context.createModel(entry.path, context.dropPoint(context.camera.getPosition(), context.camera.getForward()));
        }
        break;
    default:
        PlatformShell::openFile(entry.path);
        break;
    }
}

void ContentBrowserPanel::itemContextMenu(EditorContext& context, const AssetEntry& entry) {
    if (!ImGui::BeginPopupContextItem("##asset_context")) {
        return;
    }
    if (entry.type == AssetType::Folder) {
        if (ImGui::MenuItem(ICON_LC_FOLDER_OPEN "  Open")) {
            activate(context, entry);
        }
    } else if (entry.type == AssetType::Scene) {
        if (ImGui::MenuItem(ICON_LC_CLAPPERBOARD "  Open Scene")) {
            activate(context, entry);
        }
    } else if (entry.type == AssetType::Model) {
        ImGui::BeginDisabled(context.isPlaying());
        if (ImGui::MenuItem(ICON_LC_PLUS "  Add to Scene")) {
            activate(context, entry);
        }
        ImGui::EndDisabled();
    }
    if (entry.type != AssetType::Folder && ImGui::MenuItem(ICON_LC_EXTERNAL_LINK "  Open in Default App")) {
        PlatformShell::openFile(entry.path);
    }
    if (entry.type == AssetType::Shader && ImGui::MenuItem(ICON_LC_REFRESH_CW "  Reload Shader")) {
        ResourceManager::getInstance().reloadShadersForFile(entry.path);
    }
    ImGui::Separator();
    const std::string reveal = std::string(ICON_LC_FOLDER_SEARCH "  Show in ") + PlatformShell::fileManagerName();
    if (ImGui::MenuItem(reveal.c_str())) {
        PlatformShell::revealInFileManager(entry.path);
    }
    if (ImGui::MenuItem(ICON_LC_COPY "  Copy Path")) {
        ImGui::SetClipboardText(entry.path.c_str());
    }
    if (search_[0] != '\0' && ImGui::MenuItem(ICON_LC_FOLDER "  Show in Folder")) {
        search_[0] = '\0';
        navigate(context, AssetDatabase::parentFolder(entry.path));
        scrollToPath_ = entry.path;
    }
    ImGui::EndPopup();
}

void ContentBrowserPanel::drawGrid(EditorContext& context, const std::vector<AssetEntry>& entries) {
    const float size = std::round(tileSize * EditorTheme::uiScale());
    const float tileHeight = cardHeight(size);
    const float spacing = EditorUI::px(kTileSpacing);
    const float cellWidth = size + spacing;
    const float cellHeight = tileHeight + spacing;
    const float available = ImGui::GetContentRegionAvail().x;
    const int columns = std::max(1, static_cast<int>((available + spacing) / cellWidth));
    const ImVec2 origin(ImGui::GetCursorScreenPos().x + 2.0f, ImGui::GetCursorScreenPos().y + 2.0f);

    for (std::size_t i = 0; i < entries.size(); ++i) {
        const AssetEntry& entry = entries[i];
        const int column = static_cast<int>(i) % columns;
        const int row = static_cast<int>(i) / columns;
        const ImVec2 min(origin.x + column * cellWidth, origin.y + row * cellHeight);
        ImGui::SetCursorScreenPos(min);
        ImGui::PushID(entry.path.c_str());
        ImGui::InvisibleButton("##tile", ImVec2(size, tileHeight));
        const bool hovered = ImGui::IsItemHovered();
        if (scrollToPath_ == entry.path) {
            ImGui::SetScrollHereY(0.4f);
            scrollToPath_.clear();
        }
        handleItemInteraction(context, entry);
        if (ImGui::IsItemVisible()) {
            drawTile(context, entry, min, size, context.selectedAsset == entry.path, hovered);
        }
        ImGui::PopID();
    }
    const int rows = (static_cast<int>(entries.size()) + columns - 1) / columns;
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rows * cellHeight));
    // Остаток окна — пустое место: клик снимает выделение, ПКМ — меню папки.
    const ImVec2 rest = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##grid_blank", ImVec2(std::max(1.0f, rest.x), std::max(8.0f, rest.y)));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        context.selectedAsset.clear();
    }
    if (ImGui::BeginPopupContextItem("##folder_blank")) {
        const std::string reveal = std::string(ICON_LC_FOLDER_SEARCH "  Show in ") + PlatformShell::fileManagerName();
        if (ImGui::MenuItem(reveal.c_str())) {
            PlatformShell::revealInFileManager(currentFolder_);
        }
        if (ImGui::MenuItem(ICON_LC_REFRESH_CW "  Refresh")) {
            context.assets.refresh();
            ThumbnailCache::instance().forgetFailures();
        }
        ImGui::EndPopup();
    }
}

void ContentBrowserPanel::drawList(EditorContext& context, const std::vector<AssetEntry>& entries) {
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 3.0f));
    if (!ImGui::BeginTable("##asset_list", 4, flags)) {
        ImGui::PopStyleVar();
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableHeadersRow();
    for (const AssetEntry& entry : entries) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(entry.path.c_str());
        const bool selected = context.selectedAsset == entry.path;
        ImU32 color = 0;
        const char* icon = assetIcon(entry.type, &color);
        const ImVec2 cell = ImGui::GetCursorScreenPos();
        ImGui::Selectable("##row", selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
            ImVec2(0.0f, ImGui::GetTextLineHeight() + 4.0f));
        if (scrollToPath_ == entry.path) {
            ImGui::SetScrollHereY(0.4f);
            scrollToPath_.clear();
        }
        handleItemInteraction(context, entry);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddText(ImVec2(cell.x + 2.0f, cell.y + 2.0f), styled(color), icon);
        drawList->AddText(ImVec2(cell.x + ImGui::GetFontSize() + EditorUI::px(10.0f), cell.y + 2.0f), styled(kText), entry.name.c_str());
        // Остальные колонки — на той же базовой линии, что и имя внутри строки-Selectable.
        auto cellText = [](int column, const std::string& text, bool faint) {
            ImGui::TableSetColumnIndex(column);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
            if (faint) {
                EditorUI::textFaint(text.c_str());
            } else {
                EditorUI::textDim(text.c_str());
            }
        };
        cellText(1, AssetDatabase::typeName(entry.type), false);
        if (entry.type != AssetType::Folder) {
            cellText(2, AssetDatabase::formatSize(entry.size), false);
            cellText(3, formatTime(entry.modified), true);
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
    ImGui::PopStyleVar();
}

void ContentBrowserPanel::drawFooter(EditorContext& context, std::size_t itemCount) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetTextLineHeight() + 8.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddLine(min, ImVec2(min.x + width, min.y), styled(kBorder));
    EditorUI::pushSmallFont();
    std::string text = std::to_string(itemCount) + (itemCount == 1 ? " item" : " items");
    if (!context.selectedAsset.empty()) {
        if (const AssetEntry* entry = context.assets.find(context.selectedAsset)) {
            text += "   \xC2\xB7   " + entry->path;
            if (entry->type != AssetType::Folder) {
                text += "  (" + AssetDatabase::formatSize(entry->size) + ")";
            }
        }
    }
    const float textY = min.y + (height - ImGui::GetFontSize()) * 0.5f;
    EditorUI::drawTextEllipsis(drawList, ImVec2(min.x + 10.0f, textY), width - 20.0f, text.c_str(), kTextFaint);
    EditorUI::popFont();
    ImGui::Dummy(ImVec2(width, height));
}
