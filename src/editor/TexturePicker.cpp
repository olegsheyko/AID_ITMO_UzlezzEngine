#include "editor/TexturePicker.h"
#include "resources/ResourceManager.h"

#include <algorithm>
#include <cfloat>
#include <filesystem>

namespace {
constexpr float kThumbnailSize = 48.0f;
constexpr float kRowHeight = 60.0f;

const TextureData* loadedTexture(const std::shared_ptr<Resource<TextureData>>& resource) {
    return resource && resource->isLoaded() && resource->getData()->textureId != 0 ? resource->getData() : nullptr;
}

void drawThumbnail(const ImVec2& position, float size, const TextureData* texture) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    constexpr int kCells = 6;
    const float cell = size / kCells;
    for (int y = 0; y < kCells; ++y) {
        for (int x = 0; x < kCells; ++x) {
            const ImU32 color = (x + y) % 2 ? IM_COL32(65, 65, 65, 255) : IM_COL32(40, 40, 40, 255);
            draw->AddRectFilled(ImVec2(position.x + x * cell, position.y + y * cell),
                ImVec2(position.x + (x + 1) * cell, position.y + (y + 1) * cell), color);
        }
    }
    if (texture && texture->width > 0 && texture->height > 0) {
        const float scale = size / static_cast<float>(std::max(texture->width, texture->height));
        const ImVec2 dimensions(texture->width * scale, texture->height * scale);
        const ImVec2 start(position.x + (size - dimensions.x) * 0.5f, position.y + (size - dimensions.y) * 0.5f);
        draw->AddImage(static_cast<ImTextureID>(texture->textureId), start,
            ImVec2(start.x + dimensions.x, start.y + dimensions.y), ImVec2(0, 1), ImVec2(1, 0));
    } else {
        const ImVec2 textSize = ImGui::CalcTextSize("--");
        draw->AddText(ImVec2(position.x + (size - textSize.x) * 0.5f, position.y + (size - textSize.y) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), "--");
    }
    draw->AddRect(position, ImVec2(position.x + size, position.y + size), ImGui::GetColorU32(ImGuiCol_Border));
}
}

void TexturePicker::render(MeshRenderer& meshRenderer) {
    ResourceManager& resources = ResourceManager::getInstance();
    const TextureData* current = loadedTexture(meshRenderer.cachedBaseColorTexture);
    if (!current && meshRenderer.cachedMesh && meshRenderer.cachedMesh->isLoaded()) {
        for (const auto& subMesh : meshRenderer.cachedMesh->getData()->subMeshes) {
            current = loadedTexture(subMesh.material.cachedDiffuseTexture);
            if (current) break;
        }
    }

    ImGui::PushID("BaseTexturePicker");
    ImGui::TextUnformatted("Base Texture");
    drawThumbnail(ImGui::GetCursorScreenPos(), kThumbnailSize, current);
    ImGui::Dummy(ImVec2(kThumbnailSize, kThumbnailSize));
    ImGui::SameLine();
    ImGui::BeginGroup();
    const std::string preview = meshRenderer.baseColorTextureId.empty()
        ? "Material / default" : std::filesystem::path(meshRenderer.baseColorTextureId).filename().string();
    ImGui::SetNextItemWidth(-FLT_MIN);
    const float popupWidth = std::min(460.0f, ImGui::GetMainViewport()->WorkSize.x - 20.0f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(popupWidth, 0.0f), ImVec2(popupWidth, 480.0f));
    const bool open = ImGui::BeginCombo("##texture", preview.c_str());
    if (open) {
        const bool justOpened = ImGui::IsWindowAppearing();
        if (justOpened) {
            paths_ = resources.getAvailableTexturePaths();
            filter_.Clear();
            for (auto it = previews_.begin(); it != previews_.end();) {
                if (!loadedTexture(it->second)) it = previews_.erase(it);
                else ++it;
            }
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##search", "Search by name or folder...", filter_.InputBuf, IM_ARRAYSIZE(filter_.InputBuf))) {
            filter_.Build();
        }
        if (ImGui::Selectable("Material / default", meshRenderer.baseColorTextureId.empty())) {
            meshRenderer.baseColorTextureId.clear();
            meshRenderer.cachedBaseColorTexture.reset();
        }
        ImGui::Separator();

        std::vector<const std::string*> visiblePaths;
        for (const auto& path : paths_) {
            if (filter_.PassFilter(path.c_str())) visiblePaths.push_back(&path);
        }
        ImGui::TextDisabled("%d textures", static_cast<int>(visiblePaths.size()));
        if (visiblePaths.empty()) ImGui::TextDisabled("No textures found.");
        ImGui::BeginChild("##texture_list", ImVec2(0.0f, 330.0f));
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visiblePaths.size()), kRowHeight + ImGui::GetStyle().ItemSpacing.y);
        if (justOpened) {
            for (size_t i = 0; i < visiblePaths.size(); ++i) {
                if (*visiblePaths[i] == meshRenderer.baseColorTextureId) clipper.IncludeItemByIndex(static_cast<int>(i));
            }
        }
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& path = *visiblePaths[i];
                auto [it, inserted] = previews_.try_emplace(path);
                // Превью — самое неважное, что грузит редактор: низкий приоритет, текстуры сцены обгоняют их.
                if (inserted) it->second = resources.loadTextureAsync(path, JobPriority::Low);
                const TextureData* texture = loadedTexture(it->second);
                const bool loading = it->second && it->second->isPending();
                const bool selected = meshRenderer.baseColorTextureId == path;
                ImGui::PushID(path.c_str());
                const ImVec2 rowStart = ImGui::GetCursorScreenPos();
                const float rowWidth = ImGui::GetContentRegionAvail().x;
                const ImGuiSelectableFlags flags = texture ? 0 : ImGuiSelectableFlags_Disabled;
                if (ImGui::Selectable("##row", selected, flags, ImVec2(0.0f, kRowHeight))) {
                    meshRenderer.baseColorTextureId = path;
                    meshRenderer.cachedBaseColorTexture = it->second;
                    ImGui::CloseCurrentPopup();
                }
                if (selected && justOpened) ImGui::SetItemDefaultFocus();
                const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled);
                drawThumbnail(ImVec2(rowStart.x + 4.0f, rowStart.y + 6.0f), kThumbnailSize, texture);
                const std::filesystem::path file(path);
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const float textX = rowStart.x + kThumbnailSize + 16.0f;
                draw->PushClipRect(ImVec2(textX, rowStart.y), ImVec2(rowStart.x + rowWidth, rowStart.y + kRowHeight), true);
                draw->AddText(ImVec2(textX, rowStart.y + 9.0f), ImGui::GetColorU32(ImGuiCol_Text), file.filename().string().c_str());
                draw->AddText(ImVec2(textX, rowStart.y + 33.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    texture ? file.parent_path().generic_string().c_str() : loading ? "Loading..." : "Unable to load image");
                draw->PopClipRect();
                if (hovered) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(path.c_str());
                    if (texture) {
                        drawThumbnail(ImGui::GetCursorScreenPos(), 160.0f, texture);
                        ImGui::Dummy(ImVec2(160.0f, 160.0f));
                        ImGui::Text("%d x %d", texture->width, texture->height);
                    } else if (loading) {
                        ImGui::TextUnformatted("Loading in the background...");
                    } else {
                        ImGui::TextUnformatted("File is missing or cannot be decoded.");
                    }
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::EndCombo();
    }
    if (current) ImGui::TextDisabled("%d x %d", current->width, current->height);
    else ImGui::TextDisabled("Material color");
    ImGui::EndGroup();
    ImGui::PopID();
}
