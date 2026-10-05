#pragma once

#include "math/MathTypes.h"

#include <imgui.h>

#include <cstddef>
#include <string>

// Общие контролы редактора поверх ImGui: всё, что нужно больше чем одной панели.
namespace EditorUI {
// Размер в точках с учётом масштаба интерфейса (View → UI Scale).
float px(float value);
// Подписи хоткеев: на macOS Cmd вместо Ctrl.
std::string shortcut(const char* keys);

// Текст, обрезанный многоточием по ширине.
std::string ellipsize(const char* text, float maxWidth);
void drawTextEllipsis(ImDrawList* drawList, const ImVec2& position, float maxWidth, const char* text, ImU32 color);
void drawTextCentered(ImDrawList* drawList, const ImVec2& center, const char* text, ImU32 color);

// Иконка и подпись разными цветами в одну строку.
void iconLabel(const char* icon, const char* text, ImU32 iconColor, ImU32 textColor = 0);
void textDim(const char* text);
void textFaint(const char* text);
void helpMarker(const char* text);

// Квадратная кнопка-иконка; active подсвечивает её как включённую.
bool iconButton(const char* id, const char* icon, const char* tooltip = nullptr, bool active = false, float size = 0.0f);
// Кнопка тулбара с иконкой и необязательной подписью; active — нажатое состояние.
bool toolButton(const char* id, const char* icon, const char* label, bool active, const char* tooltip = nullptr);
void toolbarSeparator();
// Основная кнопка с акцентной заливкой.
bool primaryButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f));

// Чекбокс в стиле редактора: синяя заливка с галочкой, подпись справа (часть после ## скрыта).
bool checkbox(const char* label, bool* value);
// Комбобокс с тонкой стрелкой вместо стандартной; закрывается обычным ImGui::EndCombo().
bool beginCombo(const char* id, const char* preview, ImGuiComboFlags flags = 0);
bool combo(const char* id, int* current, const char* const items[], int count);

// Поле поиска с лупой и крестиком очистки.
bool searchBox(const char* id, char* buffer, std::size_t bufferSize, const char* hint, float width = -1.0f);

// Цветная плашка (тип ассета, счётчик).
void badge(ImDrawList* drawList, const ImVec2& min, const char* text, ImU32 background, ImU32 foreground, float fontSize = 0.0f);
ImVec2 badgeSize(const char* text, float fontSize = 0.0f);

// Сетка свойств: подпись слева, значение справа.
bool beginProperties(const char* id);
void endProperties();
// Новая строка: рисует подпись и ставит курсор в ячейку значения на всю ширину. Возвращает true по ПКМ на подписи (сброс).
bool propertyLabel(const char* label, const char* tooltip = nullptr);
bool propertyFloat(const char* label, float& value, float speed = 0.05f, float minValue = 0.0f, float maxValue = 0.0f,
    const char* format = "%.3f", float resetValue = 0.0f);
bool propertyVec3(const char* label, Vec3& value, float speed = 0.05f, float resetValue = 0.0f, const char* format = "%.3f");
bool propertyCheckbox(const char* label, bool& value);
bool propertyText(const char* label, char* buffer, std::size_t bufferSize);
void propertyValue(const char* label, const char* value);

// Карточка компонента: заголовок на всю ширину со стрелкой, иконкой и меню «…».
enum class ComponentAction {
    None,
    Reset,
    Remove,
    Extra
};
bool componentHeader(const char* id, const char* icon, const char* title, bool removable, ComponentAction& action,
    const char* extraAction = nullptr, bool defaultOpen = true);
void componentSpacing();
// Сворачиваемая секция того же вида, но без меню; закрывается тем же componentSpacing().
bool sectionHeader(const char* id, const char* icon, const char* title, bool defaultOpen = true);

// Заглушка для пустых панелей: крупная иконка и пара строк по центру.
void emptyState(const char* icon, const char* title, const char* subtitle = nullptr);

// Мини-шрифт для второстепенного текста.
void pushSmallFont();
void pushSemibold();
void pushMono();
void popFont();
} // namespace EditorUI
