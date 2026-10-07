#include "editor/TexturePicker.h"

#include "editor/EditorTheme.h"
#include "editor/EditorWidgets.h"
#include "editor/IconsLucide.h"
#include "editor/ThumbnailCache.h"
#include "resources/ResourceManager.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

using namespace EditorTheme;

namespace {
constexpr float kTile = 76.0f;
constexpr const char* kPopupId = "Select Texture##texture_picker";

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

void drawChecker(ImDrawList* drawList, const ImVec2& min, const ImVec2& max) {
    const float cell = 8.0f;
    drawList->AddRectFilled(min, max, IM_COL32(52, 52, 56, 255), 4.0f);
    drawList->PushClipRect(min, max, true);
    for (float y = min.y; y < max.y; y += cell) {
        for (float x = min.x; x < max.x; x += cell) {
            if ((static_cast<int>((x - min.x) / cell) + static_cast<int>((y - min.y) / cell)) % 2 == 0) {
                drawList->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + cell, max.x), std::min(y + cell, max.y)), IM_COL32(64, 64, 68, 255));
            }
        }
    }
    drawList->PopClipRect();
}
}

void TexturePicker::open(const std::string& current) {
    openRequested_ = true;
    current_ = current;
}

bool TexturePicker::draw(std::string& outPath) {
    if (openRequested_) {
        openRequested_ = false;
        paths_ = ResourceManager::getInstance().getAvailableTexturePaths();
        search_.fill('\0');
        scrollToCurrent_ = true;
        ThumbnailCache::instance().forgetFailures();
        ImGui::OpenPopup(kPopupId);
    }

    const ImVec2 workSize = ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowSize(ImVec2(std::min(560.0f, workSize.x - 40.0f), std::min(520.0f, workSize.y - 40.0f)), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool keepOpen = true;
    if (!ImGui::BeginPopupModal(kPopupId, &keepOpen, ImGuiWindowFlags_NoSavedSettings)) {
        return false;
    }

    bool picked = false;
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    EditorUI::searchBox("texture_search", search_.data(), search_.size(), "Search textures");
    const std::string needle = lower(search_.data());

    std::vector<const std::string*> visible;
    for (const std::string& path : paths_) {
        if (needle.empty() || lower(path).find(needle) != std::string::npos) {
            visible.push_back(&path);
        }
    }
    EditorUI::pushSmallFont();
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextFaint));
    ImGui::Text("%d textures", static_cast<int>(visible.size()));
    ImGui::PopStyleColor();
    EditorUI::popFont();

    ImGui::BeginChild("##texture_grid", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    const float spacing = 8.0f;
    const float cellWidth = kTile + spacing;
    const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / cellWidth));
    const float labelHeight = ImGui::GetTextLineHeight() + 4.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ThumbnailCache& thumbnails = ThumbnailCache::instance();

    const int total = static_cast<int>(visible.size()) + 1;
    for (int index = 0; index < total; ++index) {
        if (index % columns != 0) {
            ImGui::SameLine(0.0f, spacing);
        }
        const bool isNone = index == 0;
        const std::string path = isNone ? std::string() : *visible[index - 1];
        ImGui::PushID(index);
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton("##tile", ImVec2(kTile, kTile + labelHeight));
        const bool hovered = ImGui::IsItemHovered();
        const bool selected = path == current_;
        if (selected && scrollToCurrent_) {
            ImGui::SetScrollHereY(0.3f);
            scrollToCurrent_ = false;
        }
        if (selected || hovered) {
            drawList->AddRectFilled(ImVec2(min.x - 3.0f, min.y - 3.0f), ImVec2(min.x + kTile + 3.0f, min.y + kTile + labelHeight + 1.0f),
                ImGui::GetColorU32(selected ? kAccentSoft : IM_COL32(255, 255, 255, 12)), 6.0f);
        }
        const ImVec2 imageMin(min.x + 4.0f, min.y + 4.0f);
        const ImVec2 imageMax(min.x + kTile - 4.0f, min.y + kTile - 4.0f);
        const TextureData* texture = nullptr;
        if (isNone) {
            drawList->AddRectFilled(imageMin, imageMax, ImGui::GetColorU32(kFrame), 4.0f);
            EditorUI::drawTextCentered(drawList, ImVec2((imageMin.x + imageMax.x) * 0.5f, (imageMin.y + imageMax.y) * 0.5f), ICON_LC_BAN, kTextFaint);
        } else {
            drawChecker(drawList, imageMin, imageMax);
            texture = thumbnails.texture(path);
            if (texture) {
                const float size = imageMax.x - imageMin.x;
                const float scale = size / static_cast<float>(std::max(texture->width, texture->height));
                const ImVec2 dims(texture->width * scale, texture->height * scale);
                const ImVec2 start(imageMin.x + (size - dims.x) * 0.5f, imageMin.y + (size - dims.y) * 0.5f);
                drawList->AddImageRounded(static_cast<ImTextureID>(texture->textureId), start, ImVec2(start.x + dims.x, start.y + dims.y),
                    ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, 3.0f);
            } else {
                EditorUI::drawTextCentered(drawList, ImVec2((imageMin.x + imageMax.x) * 0.5f, (imageMin.y + imageMax.y) * 0.5f),
                    thumbnails.isLoading(path) ? ICON_LC_LOADER_CIRCLE : ICON_LC_IMAGE_OFF, kTextFaint);
            }
        }
        const std::string label = isNone ? std::string("None") : std::filesystem::path(path).stem().string();
        EditorUI::pushSmallFont();
        const std::string fitted = EditorUI::ellipsize(label.c_str(), kTile);
        const float labelWidth = ImGui::CalcTextSize(fitted.c_str()).x;
        drawList->AddText(ImVec2(min.x + (kTile - labelWidth) * 0.5f, min.y + kTile), ImGui::GetColorU32(selected ? kText : kTextDim), fitted.c_str());
        EditorUI::popFont();

        if (hovered && !isNone && ImGui::BeginTooltip()) {
            ImGui::TextUnformatted(path.c_str());
            if (texture) {
                const ImVec2 start = ImGui::GetCursorScreenPos();
                drawChecker(ImGui::GetWindowDrawList(), start, ImVec2(start.x + 180.0f, start.y + 180.0f));
                ImGui::Image(static_cast<ImTextureID>(texture->textureId), ImVec2(180.0f, 180.0f), ImVec2(0, 1), ImVec2(1, 0));
                ImGui::TextDisabled("%d x %d", texture->width, texture->height);
            }
            ImGui::EndTooltip();
        }
        if (clicked) {
            outPath = path;
            picked = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return picked;
}
