#include "editor/panels/ConsolePanel.h"

#include "editor/EditorTheme.h"
#include "editor/EditorWidgets.h"
#include "editor/EditorWindows.h"
#include "editor/IconsLucide.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

using namespace EditorTheme;

namespace {
constexpr std::size_t kMaxEntries = 5000;

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

int levelIndex(Logger::Level level) {
    switch (level) {
    case Logger::Level::INFO: return 0;
    case Logger::Level::WARN: return 1;
    case Logger::Level::ERROR: return 2;
    }
    return 0;
}

const char* levelIcon(Logger::Level level) {
    switch (level) {
    case Logger::Level::INFO: return ICON_LC_INFO;
    case Logger::Level::WARN: return ICON_LC_TRIANGLE_ALERT;
    case Logger::Level::ERROR: return ICON_LC_OCTAGON_ALERT;
    }
    return ICON_LC_INFO;
}

ImU32 levelColor(Logger::Level level) {
    switch (level) {
    case Logger::Level::INFO: return IM_COL32(130, 160, 200, 255);
    case Logger::Level::WARN: return kWarning;
    case Logger::Level::ERROR: return kError;
    }
    return kTextDim;
}

// Переключатель фильтра уровня: иконка + счётчик, подсвечен, когда уровень показан.
bool levelToggle(const char* id, const char* icon, std::size_t count, ImU32 color, bool& enabled, const char* tooltip) {
    char label[32];
    std::snprintf(label, sizeof(label), "%zu", count);
    const float height = ImGui::GetFrameHeight();
    const float width = ImGui::CalcTextSize(icon).x + ImGui::CalcTextSize(label).x + 22.0f;
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 min = ImGui::GetItemRectMin();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (enabled) {
        drawList->AddRectFilled(min, ImVec2(min.x + width, min.y + height), ImGui::GetColorU32(hovered ? kFrameHovered : kFrame), 4.0f);
    } else if (hovered) {
        drawList->AddRectFilled(min, ImVec2(min.x + width, min.y + height), ImGui::GetColorU32(IM_COL32(255, 255, 255, 10)), 4.0f);
    }
    const float textY = min.y + (height - ImGui::GetFontSize()) * 0.5f;
    drawList->AddText(ImVec2(min.x + 7.0f, textY), ImGui::GetColorU32(enabled ? color : kTextFaint), icon);
    drawList->AddText(ImVec2(min.x + 13.0f + ImGui::CalcTextSize(icon).x, textY), ImGui::GetColorU32(enabled ? kText : kTextFaint), label);
    if (clicked) {
        enabled = !enabled;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return clicked;
}
}

void ConsolePanel::poll() {
    std::vector<Logger::Entry> fresh;
    Logger::getInstance().fetchSince(cursor_, fresh);
    if (fresh.empty()) {
        return;
    }
    for (Logger::Entry& entry : fresh) {
        ++counts_[static_cast<std::size_t>(levelIndex(entry.level))];
        entries_.push_back(std::move(entry));
    }
    if (entries_.size() > kMaxEntries) {
        const std::size_t drop = entries_.size() - kMaxEntries;
        for (std::size_t i = 0; i < drop; ++i) {
            --counts_[static_cast<std::size_t>(levelIndex(entries_[i].level))];
        }
        entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(drop));
        selected_ = selected_ >= drop && selected_ != SIZE_MAX ? selected_ - drop : SIZE_MAX;
    }
    dirty_ = true;
    if (autoScroll) {
        scrollToBottom_ = true;
    }
}

void ConsolePanel::clear() {
    entries_.clear();
    rows_.clear();
    collapsedIndex_.clear();
    counts_ = {};
    selected_ = SIZE_MAX;
    dirty_ = true;
}

std::size_t ConsolePanel::count(Logger::Level level) const {
    return counts_[static_cast<std::size_t>(levelIndex(level))];
}

bool ConsolePanel::passes(const Logger::Entry& entry) const {
    const int index = levelIndex(entry.level);
    if ((index == 0 && !showInfo) || (index == 1 && !showWarnings) || (index == 2 && !showErrors)) {
        return false;
    }
    return lastSearch_.empty() || lower(entry.message).find(lastSearch_) != std::string::npos;
}

void ConsolePanel::rebuildRows() {
    rows_.clear();
    collapsedIndex_.clear();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Logger::Entry& entry = entries_[i];
        if (!passes(entry)) {
            continue;
        }
        if (collapse) {
            const std::string key = std::to_string(levelIndex(entry.level)) + entry.message;
            auto it = collapsedIndex_.find(key);
            if (it != collapsedIndex_.end()) {
                Row& row = rows_[it->second];
                ++row.repeat;
                // В свёрнутом виде строка показывает последнее время.
                row.entry = i;
                continue;
            }
            collapsedIndex_.emplace(key, rows_.size());
        }
        rows_.push_back(Row{i, 1});
    }
    dirty_ = false;
}

void ConsolePanel::draw() {
    if (!open) {
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin(EditorWindow::kConsole, &open);
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return;
    }

    drawToolbar();
    const std::string search = lower(search_.data());
    const bool filtersChanged = lastFilters_[0] != showInfo || lastFilters_[1] != showWarnings || lastFilters_[2] != showErrors;
    if (dirty_ || search != lastSearch_ || collapse != lastCollapse_ || filtersChanged) {
        lastSearch_ = search;
        lastCollapse_ = collapse;
        lastFilters_[0] = showInfo;
        lastFilters_[1] = showWarnings;
        lastFilters_[2] = showErrors;
        rebuildRows();
    }

    const bool hasDetails = selected_ < entries_.size();
    const float available = ImGui::GetContentRegionAvail().y;
    const float listHeight = hasDetails ? std::max(40.0f, available - detailsHeight_ - 6.0f) : available;
    drawRows(listHeight);
    if (hasDetails) {
        // Перетаскиваемый разделитель между списком и деталями.
        ImGui::InvisibleButton("##splitter", ImVec2(ImGui::GetContentRegionAvail().x, 6.0f));
        if (ImGui::IsItemActive()) {
            detailsHeight_ = std::clamp(detailsHeight_ - ImGui::GetIO().MouseDelta.y, 40.0f, available - 60.0f);
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        }
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, (min.y + max.y) * 0.5f), ImVec2(max.x, (min.y + max.y) * 0.5f),
            ImGui::GetColorU32(ImGui::IsItemActive() ? kAccent : kBorder));
        drawDetails();
    }
    ImGui::End();
}

void ConsolePanel::drawToolbar() {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight() + 10.0f;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, min.y + height - 1.0f), ImVec2(min.x + width, min.y + height - 1.0f), ImGui::GetColorU32(kBorder));
    ImGui::SetCursorScreenPos(ImVec2(min.x + 8.0f, min.y + 5.0f));

    if (EditorUI::toolButton("##clear", ICON_LC_LIST_X, "Clear", false, "Clear the console")) {
        clear();
    }
    ImGui::SameLine(0.0f, 4.0f);
    EditorUI::toolbarSeparator();
    if (EditorUI::toolButton("##collapse", ICON_LC_LAYERS_2, "Collapse", collapse, "Merge identical messages")) {
        collapse = !collapse;
    }
    ImGui::SameLine(0.0f, 4.0f);
    if (EditorUI::toolButton("##autoscroll", ICON_LC_ARROW_DOWN_TO_LINE, "Auto-scroll", autoScroll, "Follow new messages")) {
        autoScroll = !autoScroll;
        scrollToBottom_ = autoScroll;
    }

    const float togglesWidth = 210.0f;
    const float searchWidth = std::clamp(width * 0.3f, 120.0f, 280.0f);
    ImGui::SameLine(0.0f, 0.0f);
    const float searchX = std::max(ImGui::GetCursorScreenPos().x + 12.0f, min.x + width - togglesWidth - searchWidth - 16.0f);
    ImGui::SetCursorScreenPos(ImVec2(searchX, min.y + 5.0f));
    EditorUI::searchBox("console_search", search_.data(), search_.size(), "Filter messages", searchWidth);
    ImGui::SameLine(0.0f, 10.0f);
    levelToggle("##info", ICON_LC_INFO, counts_[0], levelColor(Logger::Level::INFO), showInfo, "Show info messages");
    ImGui::SameLine(0.0f, 2.0f);
    levelToggle("##warn", ICON_LC_TRIANGLE_ALERT, counts_[1], kWarning, showWarnings, "Show warnings");
    ImGui::SameLine(0.0f, 2.0f);
    levelToggle("##error", ICON_LC_OCTAGON_ALERT, counts_[2], kError, showErrors, "Show errors");

    ImGui::SetCursorScreenPos(ImVec2(min.x, min.y + height));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void ConsolePanel::drawRows(float height) {
    ImGui::BeginChild("##console_rows", ImVec2(0.0f, height), ImGuiChildFlags_None);
    if (rows_.empty()) {
        EditorUI::emptyState(ICON_LC_SQUARE_TERMINAL, entries_.empty() ? "Console is empty" : "No messages match the filters");
        ImGui::EndChild();
        return;
    }
    const float rowHeight = ImGui::GetTextLineHeight() + 8.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows_.size()), rowHeight);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const Row& row = rows_[static_cast<std::size_t>(i)];
            const Logger::Entry& entry = entries_[row.entry];
            ImGui::PushID(i);
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const bool selected = selected_ == row.entry;
            if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(width, rowHeight))) {
                selected_ = row.entry;
            }
            if (ImGui::BeginPopupContextItem("##row_context")) {
                if (ImGui::MenuItem(ICON_LC_COPY "  Copy Message")) {
                    ImGui::SetClipboardText(entry.message.c_str());
                }
                if (ImGui::MenuItem(ICON_LC_CLIPBOARD_COPY "  Copy Line")) {
                    ImGui::SetClipboardText((entry.time + " " + entry.message).c_str());
                }
                ImGui::EndPopup();
            }
            if (i % 2 == 1 && !selected) {
                drawList->AddRectFilled(min, ImVec2(min.x + width, min.y + rowHeight), ImGui::GetColorU32(IM_COL32(255, 255, 255, 5)));
            }
            const float textY = min.y + (rowHeight - ImGui::GetFontSize()) * 0.5f;
            const ImU32 color = levelColor(entry.level);
            if (entry.level == Logger::Level::ERROR) {
                drawList->AddRectFilled(min, ImVec2(min.x + 2.0f, min.y + rowHeight), ImGui::GetColorU32(kError));
            }
            drawList->AddText(ImVec2(min.x + 10.0f, textY), ImGui::GetColorU32(color), levelIcon(entry.level));
            EditorUI::pushMono();
            const float monoY = min.y + (rowHeight - ImGui::GetFontSize()) * 0.5f;
            drawList->AddText(ImVec2(min.x + 32.0f, monoY), ImGui::GetColorU32(kTextFaint), entry.time.c_str());
            const float timeWidth = ImGui::CalcTextSize("00:00:00").x;
            EditorUI::popFont();

            float rightEdge = min.x + width - 8.0f;
            if (row.repeat > 1) {
                char repeat[16];
                std::snprintf(repeat, sizeof(repeat), "%d", row.repeat);
                const ImVec2 badgeSize = EditorUI::badgeSize(repeat, 11.0f);
                rightEdge -= badgeSize.x;
                EditorUI::badge(drawList, ImVec2(rightEdge, min.y + (rowHeight - badgeSize.y) * 0.5f), repeat, kFrameActive, kText, 11.0f);
                rightEdge -= 8.0f;
            }
            const float messageX = min.x + 44.0f + timeWidth;
            // Сообщение одной строкой: переводы строк показываем в деталях.
            std::string firstLine = entry.message.substr(0, entry.message.find('\n'));
            const ImU32 textColor = entry.level == Logger::Level::INFO ? IM_COL32(214, 214, 218, 255) : color;
            EditorUI::drawTextEllipsis(drawList, ImVec2(messageX, textY), rightEdge - messageX, firstLine.c_str(), textColor);
            ImGui::PopID();
        }
    }
    if (scrollToBottom_ || (autoScroll && wasAtBottom && !ImGui::IsWindowFocused())) {
        ImGui::SetScrollHereY(1.0f);
        scrollToBottom_ = false;
    }
    ImGui::EndChild();
}

void ConsolePanel::drawDetails() {
    const Logger::Entry& entry = entries_[selected_];
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toVec4(IM_COL32(24, 24, 26, 255)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::BeginChild("##console_details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    EditorUI::iconLabel(levelIcon(entry.level), entry.level == Logger::Level::ERROR ? "Error" : entry.level == Logger::Level::WARN ? "Warning" : "Info",
        levelColor(entry.level));
    ImGui::SameLine();
    EditorUI::textFaint(entry.time.c_str());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight() * 2.0f - 6.0f);
    if (EditorUI::iconButton("##copy", ICON_LC_COPY, "Copy message")) {
        ImGui::SetClipboardText(entry.message.c_str());
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (EditorUI::iconButton("##close", ICON_LC_X, "Close details")) {
        selected_ = SIZE_MAX;
    }
    EditorUI::pushMono();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(entry.message.c_str());
    ImGui::PopTextWrapPos();
    EditorUI::popFont();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}
