#pragma once

#include <imgui.h>

enum class SyntaxLanguage {
    Plain,
    Glsl,
    Json,
    Lua
};

// Подсветка одной строки исходника для превью в инспекторе. Шрифт — текущий (моноширинный).
// inBlockComment — открыт ли /* */ комментарий в начале строки; на выходе — в конце.
void drawHighlightedLine(ImDrawList* drawList, const ImVec2& position, const char* begin, const char* end,
    SyntaxLanguage language, bool& inBlockComment);
// Только продвигает состояние блочного комментария через строку, ничего не рисуя.
void advanceBlockComment(const char* begin, const char* end, SyntaxLanguage language, bool& inBlockComment);
