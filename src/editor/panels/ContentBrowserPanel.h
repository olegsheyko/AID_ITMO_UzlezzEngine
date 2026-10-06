#pragma once

#include "editor/AssetDatabase.h"

#include <array>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class EditorContext;
struct ImDrawList;
struct ImVec2;

class ContentBrowserPanel {
public:
    void draw(EditorContext& context, float dt);
    void openFolder(EditorContext& context, const std::string& folder);

    bool open = true;
    // Размер плиток и режим показа — сохраняются вместе с остальными настройками редактора.
    float tileSize = 96.0f;
    bool listView = false;

private:
    void navigate(EditorContext& context, const std::string& folder, bool recordHistory = true);
    void drawToolbar(EditorContext& context);
    void drawBreadcrumbs(EditorContext& context, float maxWidth);
    void drawFolderTree(EditorContext& context, const std::string& folder, int depth);
    void drawGrid(EditorContext& context, const std::vector<AssetEntry>& entries);
    void drawList(EditorContext& context, const std::vector<AssetEntry>& entries);
    void drawTile(EditorContext& context, const AssetEntry& entry, const ImVec2& min, float size, bool selected, bool hovered);
    float cardHeight(float size) const;
    const std::pair<std::string, std::string>& wrappedName(const AssetEntry& entry, float width);
    void drawFooter(EditorContext& context, std::size_t itemCount);
    void handleItemInteraction(EditorContext& context, const AssetEntry& entry);
    void itemContextMenu(EditorContext& context, const AssetEntry& entry);
    void activate(EditorContext& context, const AssetEntry& entry);

    std::string currentFolder_;
    std::vector<std::string> history_;
    int historyIndex_ = -1;
    std::array<char, 128> search_{};
    std::string scrollToPath_;
    std::string draggingPath_;
    bool revealFolder_ = false;
    std::unordered_map<std::string, std::pair<std::string, std::string>> nameCache_;
    float refreshTimer_ = 0.0f;
    float treeWidth_ = 190.0f;
    bool initialized_ = false;
};
