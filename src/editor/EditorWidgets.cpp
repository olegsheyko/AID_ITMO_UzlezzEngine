#include "editor/EditorWidgets.h"

#include "editor/EditorTheme.h"
#include "editor/IconsLucide.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cstring>

namespace EditorUI {
namespace {
using namespace EditorTheme;

ImU32 styled(ImU32 color) {
    // Учитывает style.Alpha — иначе самодельная отрисовка не тускнеет внутри BeginDisabled.
    return ImGui::GetColorU32(color);
}

bool replacePrefix(std::string& text, const char* from, const char* to) {
    const std::size_t at = text.find(from);
    if (at == std::string::npos) {
        return false;
    }
    text.replace(at, std::strlen(from), to);
    return true;
}
}

float px(float value) {
    return value * EditorTheme::uiScale();
}

std::string shortcut(const char* keys) {
    std::string text = keys;
#ifdef __APPLE__
    ImFont* font = fonts().regular;
    const bool symbols = font && font->IsGlyphInFont(0x2318) && font->IsGlyphInFont(0x21E7);
    if (symbols) {
        replacePrefix(text, "Ctrl+", "\xE2\x8C\x98");
        replacePrefix(text, "Shift+", "\xE2\x87\xA7");
        replacePrefix(text, "Alt+", "\xE2\x8C\xA5");
    } else {
        replacePrefix(text, "Ctrl+", "Cmd+");
    }
#endif
    return text;
}

std::string ellipsize(const char* text, float maxWidth) {
    if (ImGui::CalcTextSize(text).x <= maxWidth) {
        return text;
    }
    const char* ellipsis = "...";
    const float ellipsisWidth = ImGui::CalcTextSize(ellipsis).x;
    std::string result = text;
    // UTF-8: отрезаем целые символы с конца.
    while (!result.empty()) {
        std::size_t cut = result.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(result[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        result.erase(cut);
        if (ImGui::CalcTextSize(result.c_str()).x + ellipsisWidth <= maxWidth) {
            break;
        }
    }
    return result + ellipsis;
}

void drawTextEllipsis(ImDrawList* drawList, const ImVec2& position, float maxWidth, const char* text, ImU32 color) {
    const std::string fitted = ellipsize(text, maxWidth);
    drawList->AddText(position, styled(color), fitted.c_str());
}

void drawTextCentered(ImDrawList* drawList, const ImVec2& center, const char* text, ImU32 color) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    drawList->AddText(ImVec2(std::floor(center.x - size.x * 0.5f), std::floor(center.y - size.y * 0.5f)), styled(color), text);
}

void iconLabel(const char* icon, const char* text, ImU32 iconColor, ImU32 textColor) {
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(iconColor));
    ImGui::TextUnformatted(icon);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 6.0f);
    if (textColor != 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(textColor));
    }
    ImGui::TextUnformatted(text);
    if (textColor != 0) {
        ImGui::PopStyleColor();
    }
}

void textDim(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextDim));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void textFaint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextFaint));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void helpMarker(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextFaint));
    ImGui::TextUnformatted(ICON_LC_CIRCLE_HELP);
    ImGui::PopStyleColor();
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool iconButton(const char* id, const char* icon, const char* tooltip, bool active, float size) {
    if (size <= 0.0f) {
        size = ImGui::GetFrameHeight();
    }
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 background = 0;
    if (active) {
        background = held ? IM_COL32(61, 139, 253, 90) : kAccentSoft;
    } else if (held) {
        background = kFrameActive;
    } else if (hovered) {
        background = kFrameHovered;
    }
    if (background != 0) {
        drawList->AddRectFilled(rect.Min, rect.Max, styled(background), 4.0f);
    }
    const ImU32 iconColor = active ? kAccentHovered : (hovered ? kText : kTextDim);
    drawTextCentered(drawList, rect.GetCenter(), icon, iconColor);
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return clicked;
}

bool toolButton(const char* id, const char* icon, const char* label, bool active, const char* tooltip) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetFrameHeight();
    const float iconWidth = icon ? ImGui::CalcTextSize(icon).x : 0.0f;
    const float labelWidth = label && label[0] ? ImGui::CalcTextSize(label).x : 0.0f;
    const float gap = (iconWidth > 0.0f && labelWidth > 0.0f) ? 6.0f : 0.0f;
    const float padding = labelWidth > 0.0f ? style.FramePadding.x : (height - iconWidth) * 0.5f;
    const float width = std::max(height, iconWidth + gap + labelWidth + padding * 2.0f);

    const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 background = 0;
    if (active) {
        background = kAccentSoft;
    } else if (held) {
        background = kFrameActive;
    } else if (hovered) {
        background = kFrameHovered;
    }
    if (background != 0) {
        drawList->AddRectFilled(rect.Min, rect.Max, styled(background), 4.0f);
    }
    const ImU32 color = active ? kAccentHovered : (hovered ? kText : kTextDim);
    const float startX = rect.Min.x + (width - (iconWidth + gap + labelWidth)) * 0.5f;
    const float textY = rect.Min.y + (height - ImGui::GetFontSize()) * 0.5f;
    if (icon) {
        drawList->AddText(ImVec2(std::floor(startX), std::floor(textY)), styled(color), icon);
    }
    if (labelWidth > 0.0f) {
        drawList->AddText(ImVec2(std::floor(startX + iconWidth + gap), std::floor(textY)),
            styled(active ? kText : color), label);
    }
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return clicked;
}

void toolbarSeparator() {
    ImGui::SameLine(0.0f, 6.0f);
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(position.x, position.y + height * 0.2f),
        ImVec2(position.x, position.y + height * 0.8f),
        styled(kBorderStrong), 1.0f);
    ImGui::Dummy(ImVec2(1.0f, height));
    ImGui::SameLine(0.0f, 6.0f);
}

bool primaryButton(const char* label, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_Button, toVec4(kAccent));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toVec4(kAccentHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, toVec4(IM_COL32(48, 118, 230, 255)));
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(IM_COL32(255, 255, 255, 255)));
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return clicked;
}

bool checkbox(const char* label, bool* value) {
    ImGui::PushID(label);
    const float frameHeight = ImGui::GetFrameHeight();
    const float size = std::floor(frameHeight - 6.0f);
    const char* labelEnd = ImGui::FindRenderedTextEnd(label);
    const float labelWidth = labelEnd > label ? ImGui::CalcTextSize(label, labelEnd).x : 0.0f;
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float width = size + (labelWidth > 0.0f ? 7.0f + labelWidth : 0.0f);
    const bool clicked = ImGui::InvisibleButton("##checkbox", ImVec2(width, frameHeight));
    if (clicked) {
        *value = !*value;
        ImGui::MarkItemEdited(ImGui::GetItemID());
    }
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 boxMin(position.x, position.y + (frameHeight - size) * 0.5f);
    const ImVec2 boxMax(boxMin.x + size, boxMin.y + size);
    if (*value) {
        const ImU32 fill = held ? IM_COL32(48, 118, 230, 255) : (hovered ? kAccentHovered : kAccent);
        drawList->AddRectFilled(boxMin, boxMax, styled(fill), 4.0f);
        ImGui::PushFont(fonts().semibold, size * 0.82f);
        drawTextCentered(drawList, ImVec2((boxMin.x + boxMax.x) * 0.5f, (boxMin.y + boxMax.y) * 0.5f + 0.5f), ICON_LC_CHECK, IM_COL32_WHITE);
        ImGui::PopFont();
    } else {
        drawList->AddRectFilled(boxMin, boxMax, styled(held ? kFrameActive : (hovered ? kFrameHovered : kFrame)), 4.0f);
        drawList->AddRect(boxMin, boxMax, styled(hovered ? IM_COL32(96, 96, 104, 255) : kBorderStrong), 4.0f, 0, 1.0f);
    }
    if (labelWidth > 0.0f) {
        drawList->AddText(ImVec2(boxMax.x + 7.0f, position.y + (frameHeight - ImGui::GetFontSize()) * 0.5f), styled(kText), label, labelEnd);
    }
    ImGui::PopID();
    return clicked;
}

bool beginCombo(const char* id, const char* preview, ImGuiComboFlags flags) {
    // Begin() попапа перезаписывает данные последнего элемента — запоминаем геометрию заранее.
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float width = ImGui::CalcItemWidth();
    const float height = ImGui::GetFrameHeight();
    // Превью короче поля на ширину стрелки, иначе текст заезжает под неё.
    const std::string fitted = ellipsize(preview, width - height - ImGui::GetStyle().FramePadding.x * 2.0f);
    const bool open = ImGui::BeginCombo(id, fitted.c_str(), flags | ImGuiComboFlags_NoArrowButton);
    const float chevronX = position.x + width - height * 0.5f - 2.0f;
    drawTextCentered(drawList, ImVec2(chevronX, position.y + height * 0.5f), open ? ICON_LC_CHEVRON_UP : ICON_LC_CHEVRON_DOWN, kTextDim);
    return open;
}

bool combo(const char* id, int* current, const char* const items[], int count) {
    bool changed = false;
    if (beginCombo(id, (*current >= 0 && *current < count) ? items[*current] : "")) {
        for (int i = 0; i < count; ++i) {
            if (ImGui::Selectable(items[i], *current == i)) {
                *current = i;
                changed = true;
            }
            if (*current == i) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool searchBox(const char* id, char* buffer, std::size_t bufferSize, const char* hint, float width) {
    ImGui::PushID(id);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float iconWidth = ImGui::CalcTextSize(ICON_LC_SEARCH).x;
    if (width < 0.0f) {
        width = ImGui::GetContentRegionAvail().x;
    }
    ImGui::SetNextItemWidth(width);
    ImGui::SetNextItemAllowOverlap();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x + iconWidth + 6.0f, style.FramePadding.y));
    bool changed = ImGui::InputTextWithHint("##search", hint, buffer, bufferSize);
    ImGui::PopStyleVar();
    const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const bool focused = ImGui::IsItemActive();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (focused) {
        drawList->AddRect(rect.Min, rect.Max, styled(withAlpha(kAccent, 0.7f)), style.FrameRounding, 0, 1.0f);
    }
    drawList->AddText(ImVec2(rect.Min.x + style.FramePadding.x, rect.Min.y + style.FramePadding.y),
        styled(focused ? kTextDim : kTextFaint), ICON_LC_SEARCH);
    if (buffer[0] != '\0') {
        // Крестик — последний элемент строки и стоит ровно у правого края поля,
        // поэтому SameLine() после поиска продолжает ту же строку.
        const float size = rect.GetHeight();
        ImGui::SetCursorScreenPos(ImVec2(rect.Max.x - size, rect.Min.y));
        if (ImGui::InvisibleButton("##clear", ImVec2(size, size))) {
            buffer[0] = '\0';
            changed = true;
        }
        const bool hovered = ImGui::IsItemHovered();
        drawTextCentered(drawList, ImVec2(rect.Max.x - size * 0.5f, rect.GetCenter().y), ICON_LC_X, hovered ? kText : kTextFaint);
    }
    ImGui::PopID();
    return changed;
}

ImVec2 badgeSize(const char* text, float fontSize) {
    ImGui::PushFont(fonts().semibold, fontSize > 0.0f ? fontSize : 10.0f);
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    ImGui::PopFont();
    return ImVec2(textSize.x + 10.0f, textSize.y + 3.0f);
}

void badge(ImDrawList* drawList, const ImVec2& min, const char* text, ImU32 background, ImU32 foreground, float fontSize) {
    ImGui::PushFont(fonts().semibold, fontSize > 0.0f ? fontSize : 10.0f);
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 max(min.x + textSize.x + 10.0f, min.y + textSize.y + 3.0f);
    drawList->AddRectFilled(min, max, styled(background), 3.0f);
    drawList->AddText(ImVec2(min.x + 5.0f, min.y + 1.5f), styled(foreground), text);
    ImGui::PopFont();
}

bool beginProperties(const char* id) {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 2.0f));
    const bool open = ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings);
    if (!open) {
        ImGui::PopStyleVar();
        return false;
    }
    const float labelWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.36f, 72.0f, 150.0f);
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void endProperties() {
    ImGui::EndTable();
    ImGui::PopStyleVar();
}

bool propertyLabel(const char* label, const char* tooltip) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    const float width = ImGui::GetContentRegionAvail().x;
    const std::string fitted = ellipsize(label, width);
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextDim));
    ImGui::TextUnformatted(fitted.c_str());
    ImGui::PopStyleColor();
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    bool reset = false;
    ImGui::PushID(label);
    if (ImGui::BeginPopupContextItem("##reset")) {
        if (ImGui::MenuItem(ICON_LC_ROTATE_CCW "  Reset")) {
            reset = true;
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    return reset;
}

bool propertyFloat(const char* label, float& value, float speed, float minValue, float maxValue, const char* format, float resetValue) {
    ImGui::PushID(label);
    bool changed = false;
    if (propertyLabel(label)) {
        value = resetValue;
        changed = true;
    }
    const ImGuiSliderFlags flags = minValue < maxValue ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
    changed |= ImGui::DragFloat("##value", &value, speed, minValue, maxValue, format, flags);
    ImGui::PopID();
    return changed;
}

bool propertyVec3(const char* label, Vec3& value, float speed, float resetValue, const char* format) {
    ImGui::PushID(label);
    bool changed = false;
    if (propertyLabel(label)) {
        value = Vec3{resetValue, resetValue, resetValue};
        changed = true;
    }
    const float spacing = 4.0f;
    const float width = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;
    float* components[3] = {&value.x, &value.y, &value.z};
    const ImU32 colors[3] = {kAxisX, kAxisY, kAxisZ};
    const char* ids[3] = {"##x", "##y", "##z"};
    for (int i = 0; i < 3; ++i) {
        if (i > 0) {
            ImGui::SameLine(0.0f, spacing);
        }
        ImGui::SetNextItemWidth(width);
        ImGui::SetNextItemColorMarker(colors[i]);
        changed |= ImGui::DragFloat(ids[i], components[i], speed, 0.0f, 0.0f, format);
    }
    ImGui::PopID();
    return changed;
}

bool propertyCheckbox(const char* label, bool& value) {
    ImGui::PushID(label);
    propertyLabel(label);
    const bool changed = checkbox("##value", &value);
    ImGui::PopID();
    return changed;
}

bool propertyText(const char* label, char* buffer, std::size_t bufferSize) {
    ImGui::PushID(label);
    propertyLabel(label);
    const bool changed = ImGui::InputText("##value", buffer, bufferSize);
    ImGui::PopID();
    return changed;
}

void propertyValue(const char* label, const char* value) {
    ImGui::PushID(label);
    propertyLabel(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(value);
    ImGui::PopID();
}

namespace {
bool cardHeader(const char* id, const char* icon, const char* title, bool withMenu, bool removable, ComponentAction& action,
    const char* extraAction, bool defaultOpen) {
    action = ComponentAction::None;
    ImGui::PushID(id);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID openId = ImGui::GetID("##open");
    bool open = storage->GetBool(openId, defaultOpen);

    const float height = ImGui::GetFrameHeight() + px(4.0f);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 max(min.x + width, min.y + height);
    const float menuSize = height - px(6.0f);

    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InvisibleButton("##header", ImVec2(width, height))) {
        open = !open;
        storage->SetBool(openId, open);
    }
    const bool hovered = ImGui::IsItemHovered();
    bool openMenu = ImGui::IsItemClicked(ImGuiMouseButton_Right);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, styled(hovered ? IM_COL32(44, 44, 47, 255) : kPanelRaised), 5.0f);
    const float textY = min.y + (height - ImGui::GetFontSize()) * 0.5f;
    const float glyph = ImGui::GetFontSize();
    drawList->AddText(ImVec2(min.x + px(7.0f), textY), styled(kTextFaint), open ? ICON_LC_CHEVRON_DOWN : ICON_LC_CHEVRON_RIGHT);
    drawList->AddText(ImVec2(min.x + px(13.0f) + glyph, textY), styled(IM_COL32(150, 180, 230, 255)), icon);
    ImGui::PushFont(fonts().semibold, 0.0f);
    drawList->AddText(ImVec2(min.x + px(21.0f) + glyph * 2.0f, textY), styled(kText), title);
    ImGui::PopFont();

    if (withMenu) {
        ImGui::SetCursorScreenPos(ImVec2(max.x - menuSize - 3.0f, min.y + 3.0f));
        if (iconButton("##menu", ICON_LC_ELLIPSIS, nullptr, false, menuSize)) {
            openMenu = true;
        }
    } else {
        openMenu = false;
    }
    if (openMenu) {
        ImGui::OpenPopup("##component_menu");
    }
    if (ImGui::BeginPopup("##component_menu")) {
        if (ImGui::MenuItem(ICON_LC_ROTATE_CCW "  Reset")) {
            action = ComponentAction::Reset;
        }
        if (extraAction && ImGui::MenuItem(extraAction)) {
            action = ComponentAction::Extra;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_LC_TRASH_2 "  Remove Component", nullptr, false, removable)) {
            action = ComponentAction::Remove;
        }
        ImGui::EndPopup();
    }
    ImGui::SetCursorScreenPos(ImVec2(min.x, max.y + 4.0f));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::PopID();
    if (open) {
        ImGui::Indent(6.0f);
    }
    return open;
}
}

bool componentHeader(const char* id, const char* icon, const char* title, bool removable, ComponentAction& action,
    const char* extraAction, bool defaultOpen) {
    return cardHeader(id, icon, title, true, removable, action, extraAction, defaultOpen);
}

bool sectionHeader(const char* id, const char* icon, const char* title, bool defaultOpen) {
    ComponentAction ignored;
    return cardHeader(id, icon, title, false, false, ignored, nullptr, defaultOpen);
}

void componentSpacing() {
    ImGui::Unindent(6.0f);
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
}

void emptyState(const char* icon, const char* title, const char* subtitle) {
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float iconSize = 34.0f;
    float blockHeight = iconSize + 10.0f + ImGui::GetFontSize();
    if (subtitle) {
        blockHeight += ImGui::GetFontSize() + 4.0f;
    }
    const float centerX = start.x + available.x * 0.5f;
    float y = start.y + std::max(8.0f, (available.y - blockHeight) * 0.45f);

    ImGui::PushFont(nullptr, iconSize);
    drawTextCentered(drawList, ImVec2(centerX, y + iconSize * 0.5f), icon, IM_COL32(80, 80, 86, 255));
    ImGui::PopFont();
    y += iconSize + 10.0f;
    ImGui::PushFont(fonts().semibold, 0.0f);
    drawTextCentered(drawList, ImVec2(centerX, y + ImGui::GetFontSize() * 0.5f), title, kTextDim);
    ImGui::PopFont();
    y += ImGui::GetFontSize() + 4.0f;
    if (subtitle) {
        ImGui::PushFont(nullptr, kSmallFontSize);
        const std::string fitted = ellipsize(subtitle, available.x - 16.0f);
        drawTextCentered(drawList, ImVec2(centerX, y + ImGui::GetFontSize() * 0.5f), fitted.c_str(), kTextFaint);
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(available.x, std::max(available.y - 1.0f, blockHeight)));
}

void pushSmallFont() {
    ImGui::PushFont(nullptr, kSmallFontSize);
}

void pushSemibold() {
    ImGui::PushFont(fonts().semibold, 0.0f);
}

void pushMono() {
    ImGui::PushFont(fonts().mono, kMonoFontSize);
}

void popFont() {
    ImGui::PopFont();
}
} // namespace EditorUI
