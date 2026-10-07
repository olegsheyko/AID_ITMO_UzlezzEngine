#include "editor/panels/GameplayPanel.h"

#include "editor/EditorContext.h"
#include "editor/EditorIcons.h"
#include "editor/EditorTheme.h"
#include "editor/EditorWidgets.h"
#include "editor/EditorWindows.h"
#include "editor/IconsLucide.h"

#include <imgui.h>
#include <lua.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

using namespace EditorTheme;
using EditorUI::px;

namespace {
// Статус Lua вида «Core HP: 10 | Wave: 2 | ... | Space: next wave» → пары «ключ: число» и подсказки.
struct StatusParts {
    std::vector<std::pair<std::string, std::string>> metrics;
    std::vector<std::string> hints;
};

std::string trim(const std::string& text) {
    const std::size_t begin = text.find_first_not_of(' ');
    if (begin == std::string::npos) {
        return {};
    }
    return text.substr(begin, text.find_last_not_of(' ') - begin + 1);
}

StatusParts parseStatus(const std::string& status) {
    StatusParts parts;
    std::size_t start = 0;
    while (start <= status.size()) {
        const std::size_t bar = status.find('|', start);
        const std::string segment = trim(status.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
        const std::size_t colon = segment.find(':');
        const std::string value = colon == std::string::npos ? std::string() : trim(segment.substr(colon + 1));
        const bool numeric = !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c) || c == '-'; });
        if (numeric) {
            parts.metrics.emplace_back(trim(segment.substr(0, colon)), value);
        } else if (!segment.empty()) {
            parts.hints.push_back(segment);
        }
        if (bar == std::string::npos) {
            break;
        }
        start = bar + 1;
    }
    return parts;
}

// Плитка метрики: крупное число и подпись, слева цветная полоска.
void metricTile(const char* label, const char* value, ImU32 accent, float width) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float height = px(50.0f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, ImVec2(min.x + width, min.y + height), ImGui::GetColorU32(kPanelRaised), 6.0f);
    drawList->AddRectFilled(min, ImVec2(min.x + px(3.0f), min.y + height), ImGui::GetColorU32(accent), 6.0f, ImDrawFlags_RoundCornersLeft);
    ImGui::PushFont(fonts().semibold, px(19.0f));
    drawList->AddText(ImVec2(min.x + px(12.0f), min.y + px(5.0f)), ImGui::GetColorU32(kText), value);
    ImGui::PopFont();
    EditorUI::pushSmallFont();
    EditorUI::drawTextEllipsis(drawList, ImVec2(min.x + px(12.0f), min.y + px(30.0f)), width - px(18.0f), label, kTextDim);
    EditorUI::popFont();
    ImGui::Dummy(ImVec2(width, height));
}

// Клавиша в рамке и описание справа.
void keyHint(const char* key, const char* text) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    EditorUI::pushSemibold();
    const ImVec2 keySize = ImGui::CalcTextSize(key);
    const float boxWidth = std::max(keySize.x + px(14.0f), px(26.0f));
    const float boxHeight = ImGui::GetFrameHeight() - px(2.0f);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + boxWidth, min.y + boxHeight);
    drawList->AddRectFilled(ImVec2(min.x, min.y + px(2.0f)), ImVec2(max.x, max.y + px(2.0f)), ImGui::GetColorU32(IM_COL32(18, 18, 20, 255)), 4.0f);
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(kPanelRaised), 4.0f);
    drawList->AddRect(min, max, ImGui::GetColorU32(kBorderStrong), 4.0f);
    drawList->AddText(ImVec2(min.x + (boxWidth - keySize.x) * 0.5f, min.y + (boxHeight - keySize.y) * 0.5f), ImGui::GetColorU32(kText), key);
    EditorUI::popFont();
    ImGui::Dummy(ImVec2(boxWidth, boxHeight + px(2.0f)));
    ImGui::SameLine(0.0f, px(10.0f));
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextDim));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

// Плашка сообщения: цветная рамка, иконка и перенос строк.
void messageBox(const char* icon, const std::string& text, ImU32 color) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float width = ImGui::GetContentRegionAvail().x;
    const float padding = px(8.0f);
    const float iconWidth = px(22.0f);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - padding);
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str(), nullptr, false, width - padding * 2.0f - iconWidth);
    const float height = textSize.y + padding * 2.0f;
    drawList->AddRectFilled(min, ImVec2(min.x + width, min.y + height), ImGui::GetColorU32(withAlpha(color, 0.10f)), 5.0f);
    drawList->AddRect(min, ImVec2(min.x + width, min.y + height), ImGui::GetColorU32(withAlpha(color, 0.40f)), 5.0f);
    drawList->AddText(ImVec2(min.x + padding, min.y + padding), ImGui::GetColorU32(color), icon);
    ImGui::SetCursorScreenPos(ImVec2(min.x + padding + iconWidth, min.y + padding));
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kText));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::SetCursorScreenPos(ImVec2(min.x, min.y + height));
    ImGui::Dummy(ImVec2(width, px(4.0f)));
}
}

void GameplayPanel::draw(EditorContext& context) {
    if (!open) {
        return;
    }
    if (!ImGui::Begin(EditorWindow::kGameplay, &open)) {
        ImGui::End();
        return;
    }
    drawRuntime(context);
    drawStatus(context);
    drawControls();
    drawScriptedEntities(context);
    ImGui::End();
}

void GameplayPanel::drawRuntime(EditorContext& context) {
    if (!EditorUI::sectionHeader("lua_runtime", ICON_LC_FILE_CODE, "Lua  \xC2\xB7  Lab 2")) {
        return;
    }
    const bool playing = context.isPlaying();
    if (EditorUI::beginProperties("##lua_runtime_table")) {
        EditorUI::propertyValue("Runtime", LUA_RELEASE "  \xC2\xB7  sol2 3.3.1");
        EditorUI::propertyLabel("State");
        ImGui::AlignTextToFramePadding();
        if (playing && context.scripts.running()) {
            ImGui::TextColored(toVec4(kSuccess), ICON_LC_PLAY "  Running  \xC2\xB7  %zu instances", context.scripts.instanceCount());
        } else {
            ImGui::TextColored(toVec4(kTextDim), ICON_LC_PAUSE "  Idle until Play");
        }
        EditorUI::propertyValue("Scene", context.sceneName().c_str());
        EditorUI::endProperties();
    }
    ImGui::Dummy(ImVec2(0.0f, px(2.0f)));
    ImGui::BeginDisabled(playing);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float half = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
    if (EditorUI::primaryButton(ICON_LC_SWORDS "  Open Arena", ImVec2(half, ImGui::GetFrameHeight() + px(4.0f)))) {
        context.loadArenaScene();
    }
    ImGui::SetItemTooltip("Replace the current scene with the Lua arena: core, wave spawner and an enemy prefab preview.");
    ImGui::SameLine();
    if (ImGui::Button(ICON_LC_REFRESH_CW "  Reload Scripts", ImVec2(-FLT_MIN, ImGui::GetFrameHeight() + px(4.0f)))) {
        context.reloadScripts();
    }
    ImGui::SetItemTooltip("Re-run the .lua files and validate classes. On error the previous classes stay active.");
    ImGui::EndDisabled();
    EditorUI::componentSpacing();
}

void GameplayPanel::drawStatus(EditorContext& context) {
    if (!EditorUI::sectionHeader("game_status", ICON_LC_CASTLE, "Status")) {
        return;
    }
    const std::string& error = context.scripts.error();
    if (context.isPlaying() && !context.scripts.status().empty()) {
        const std::string& status = context.scripts.status();
        const StatusParts parts = parseStatus(status);
        if (status.rfind("DEFEAT", 0) == 0) {
            messageBox(ICON_LC_SKULL, status, kError);
        } else if (!parts.metrics.empty()) {
            const int columns = std::max(1, std::min(static_cast<int>(parts.metrics.size()),
                static_cast<int>(ImGui::GetContentRegionAvail().x / px(92.0f))));
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float tileWidth = (ImGui::GetContentRegionAvail().x - spacing * (columns - 1)) / columns;
            for (std::size_t i = 0; i < parts.metrics.size(); ++i) {
                const std::string& label = parts.metrics[i].first;
                const std::string& value = parts.metrics[i].second;
                ImU32 accent = kAccent;
                if (label.find("HP") != std::string::npos) {
                    accent = value == "0" ? kError : kSuccess;
                } else if (label == "Alive") {
                    accent = kWarning;
                }
                if (i % static_cast<std::size_t>(columns) != 0) {
                    ImGui::SameLine();
                }
                metricTile(label.c_str(), value.c_str(), accent, tileWidth);
            }
            for (const std::string& hint : parts.hints) {
                EditorUI::textDim(hint.c_str());
            }
        } else {
            messageBox(ICON_LC_INFO, status, kAccent);
        }
    } else if (!context.scriptMessage.empty() && !context.scriptMessageIsError) {
        messageBox(ICON_LC_INFO, context.scriptMessage, kAccent);
    } else if (!context.isPlaying() && context.scriptMessage.empty() && error.empty()) {
        EditorUI::textFaint(context.isArenaScene() ? "Press Play, then click the Game view to give it keyboard focus."
                                                   : "Open the Arena to try the Lua wave defense.");
    }
    if (!error.empty()) {
        messageBox(ICON_LC_CIRCLE_ALERT, "Lua error: " + error, kError);
        if (ImGui::SmallButton(ICON_LC_COPY "  Copy error")) {
            ImGui::SetClipboardText(error.c_str());
        }
    } else if (context.scriptMessageIsError && !context.scriptMessage.empty()) {
        messageBox(ICON_LC_CIRCLE_ALERT, context.scriptMessage, kError);
    }
    EditorUI::componentSpacing();
}

void GameplayPanel::drawControls() {
    if (!EditorUI::sectionHeader("game_controls", ICON_LC_KEYBOARD, "Controls")) {
        return;
    }
    keyHint("Space", "Start the next wave");
    keyHint("F", "Defense pulse around the core");
    keyHint(ICON_LC_MOUSE_POINTER_2, "Click the Game view to give it keyboard focus");
    EditorUI::pushSmallFont();
    EditorUI::textFaint("Stop restores the scene as it was before Play.");
    EditorUI::popFont();
    EditorUI::componentSpacing();
}

void GameplayPanel::drawScriptedEntities(EditorContext& context) {
    if (!EditorUI::sectionHeader("scripted_entities", ICON_LC_LIST_TREE, "Scripted Entities")) {
        return;
    }
    int count = 0;
    for (Entity entity : context.world.getEntities()) {
        if (!context.world.hasComponent<ScriptComponent>(entity) || context.isEditorEntity(entity)) {
            continue;
        }
        if (++count > 64) {
            break;
        }
        const ScriptComponent& script = context.world.getComponent<ScriptComponent>(entity);
        ImU32 iconColor = 0;
        const char* icon = entityIcon(context, entity, &iconColor);
        ImGui::PushID(static_cast<int>(entity));
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float rowHeight = ImGui::GetFrameHeight();
        if (ImGui::Selectable("##entity", context.selected == entity, ImGuiSelectableFlags_None, ImVec2(0.0f, rowHeight))) {
            context.select(entity);
        }
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float textY = start.y + (rowHeight - ImGui::GetFontSize()) * 0.5f;
        drawList->AddText(ImVec2(start.x + px(4.0f), textY), iconColor, icon);
        const float right = start.x + ImGui::GetContentRegionAvail().x;
        const std::string className = script.className;
        EditorUI::pushSmallFont();
        const float classWidth = ImGui::CalcTextSize(className.c_str()).x;
        drawList->AddText(ImVec2(right - classWidth - px(6.0f), start.y + (rowHeight - ImGui::GetFontSize()) * 0.5f),
            ImGui::GetColorU32(kTextFaint), className.c_str());
        EditorUI::popFont();
        EditorUI::drawTextEllipsis(drawList, ImVec2(start.x + px(26.0f), textY), right - start.x - classWidth - px(40.0f),
            context.displayName(entity).c_str(), kText);
        ImGui::PopID();
    }
    if (count == 0) {
        EditorUI::textFaint("No entities with a Lua script in this scene.");
    }
    EditorUI::componentSpacing();
}
