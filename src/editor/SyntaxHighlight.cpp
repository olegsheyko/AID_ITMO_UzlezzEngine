#include "editor/SyntaxHighlight.h"

#include <cctype>
#include <cstring>
#include <string>

namespace {
constexpr ImU32 kPlain = IM_COL32(212, 212, 216, 255);
constexpr ImU32 kKeyword = IM_COL32(197, 134, 192, 255);
constexpr ImU32 kType = IM_COL32(78, 201, 176, 255);
constexpr ImU32 kNumber = IM_COL32(181, 206, 168, 255);
constexpr ImU32 kComment = IM_COL32(106, 153, 85, 255);
constexpr ImU32 kPreprocessor = IM_COL32(215, 186, 125, 255);
constexpr ImU32 kString = IM_COL32(206, 145, 120, 255);
constexpr ImU32 kFunction = IM_COL32(220, 220, 170, 255);
constexpr ImU32 kKey = IM_COL32(156, 220, 254, 255);
constexpr ImU32 kPunctuation = IM_COL32(150, 150, 158, 255);

bool isAny(const std::string& word, const char* const* list) {
    for (; *list; ++list) {
        if (word == *list) {
            return true;
        }
    }
    return false;
}

const char* const kGlslKeywords[] = {"if", "else", "for", "while", "do", "return", "break", "continue", "discard", "in", "out",
    "inout", "uniform", "layout", "const", "struct", "precision", "highp", "mediump", "lowp", "true", "false", "flat",
    "smooth", "std140", "location", "binding", nullptr};
const char* const kGlslTypes[] = {"void", "bool", "int", "uint", "float", "double", "vec2", "vec3", "vec4", "ivec2", "ivec3",
    "ivec4", "uvec2", "uvec3", "uvec4", "bvec2", "bvec3", "bvec4", "mat2", "mat3", "mat4", "sampler2D", "samplerCube",
    "sampler2DShadow", "sampler3D", nullptr};
const char* const kJsonKeywords[] = {"true", "false", "null", nullptr};
const char* const kLuaKeywords[] = {"and", "break", "do", "else", "elseif", "end", "for", "function", "goto", "if", "in",
    "local", "not", "or", "repeat", "return", "then", "until", "while", nullptr};
const char* const kLuaConstants[] = {"nil", "true", "false", "self", nullptr};

struct Cursor {
    ImDrawList* drawList;
    ImVec2 position;
    void emit(const char* begin, const char* end, ImU32 color) {
        if (begin >= end) {
            return;
        }
        if (drawList) {
            drawList->AddText(position, color, begin, end);
        }
        position.x += ImGui::CalcTextSize(begin, end).x;
    }
};

void highlight(Cursor* cursor, const char* p, const char* end, SyntaxLanguage language, bool& inBlockComment) {
    const char* const begin = p;
    auto emit = [cursor](const char* a, const char* b, ImU32 color) {
        if (cursor) {
            cursor->emit(a, b, color);
        }
    };
    if (language == SyntaxLanguage::Plain) {
        emit(p, end, kPlain);
        return;
    }
    while (p < end) {
        if (inBlockComment) {
            const char* close = p;
            while (close + 1 < end && !(close[0] == '*' && close[1] == '/')) {
                ++close;
            }
            if (close + 1 < end) {
                emit(p, close + 2, kComment);
                p = close + 2;
                inBlockComment = false;
            } else {
                emit(p, end, kComment);
                return;
            }
            continue;
        }
        const char c = *p;
        if (language == SyntaxLanguage::Glsl && c == '/' && p + 1 < end && p[1] == '/') {
            emit(p, end, kComment);
            return;
        }
        if (language == SyntaxLanguage::Glsl && c == '/' && p + 1 < end && p[1] == '*') {
            inBlockComment = true;
            emit(p, p + 2, kComment);
            p += 2;
            continue;
        }
        // В Lua «--» до конца строки; блочные --[[ ]] тоже считаем строчными — для превью этого хватает.
        if (language == SyntaxLanguage::Lua && c == '-' && p + 1 < end && p[1] == '-') {
            emit(p, end, kComment);
            return;
        }
        if (language == SyntaxLanguage::Glsl && c == '#') {
            emit(p, end, kPreprocessor);
            return;
        }
        if (c == '"' || (c == '\'' && language == SyntaxLanguage::Lua)) {
            const char* q = p + 1;
            while (q < end && *q != c) {
                q += (*q == '\\' && q + 1 < end) ? 2 : 1;
            }
            q = q < end ? q + 1 : end;
            // В JSON строка перед двоеточием — ключ.
            const char* next = q;
            while (next < end && (*next == ' ' || *next == '\t')) {
                ++next;
            }
            const bool isKey = language == SyntaxLanguage::Json && next < end && *next == ':';
            emit(p, q, isKey ? kKey : kString);
            p = q;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || (c == '-' && language == SyntaxLanguage::Json && p + 1 < end &&
                std::isdigit(static_cast<unsigned char>(p[1]))) || (c == '.' && p + 1 < end && std::isdigit(static_cast<unsigned char>(p[1])))) {
            const char* q = p + 1;
            while (q < end && (std::isalnum(static_cast<unsigned char>(*q)) || *q == '.' || ((*q == '-' || *q == '+') && (q[-1] == 'e' || q[-1] == 'E')))) {
                ++q;
            }
            emit(p, q, kNumber);
            p = q;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            const char* q = p + 1;
            while (q < end && (std::isalnum(static_cast<unsigned char>(*q)) || *q == '_')) {
                ++q;
            }
            const std::string word(p, q);
            ImU32 color = kPlain;
            if (language == SyntaxLanguage::Glsl) {
                const char* next = q;
                while (next < end && *next == ' ') {
                    ++next;
                }
                if (isAny(word, kGlslTypes)) {
                    color = kType;
                } else if (isAny(word, kGlslKeywords)) {
                    color = kKeyword;
                } else if (next < end && *next == '(') {
                    color = kFunction;
                }
            } else if (language == SyntaxLanguage::Lua) {
                const char* next = q;
                while (next < end && *next == ' ') {
                    ++next;
                }
                // Метод после «:» и вызов перед «(», «{» или строкой — как функции.
                const bool called = next < end && (*next == '(' || *next == '{' || *next == '"');
                const bool method = p > begin && p[-1] == ':';
                if (isAny(word, kLuaKeywords)) {
                    color = kKeyword;
                } else if (isAny(word, kLuaConstants)) {
                    color = kType;
                } else if (called || method) {
                    color = kFunction;
                }
            } else if (isAny(word, kJsonKeywords)) {
                color = kKeyword;
            }
            emit(p, q, color);
            p = q;
            continue;
        }
        const char* q = p + 1;
        // Многобайтовый UTF-8 символ — целиком, иначе ImGui получит обрывок.
        while (q < end && (static_cast<unsigned char>(*q) & 0xC0) == 0x80) {
            ++q;
        }
        const bool punctuation = std::strchr("{}[]();,.=+-*/<>!&|:?", c) != nullptr;
        emit(p, q, punctuation ? kPunctuation : kPlain);
        p = q;
    }
}
}

void drawHighlightedLine(ImDrawList* drawList, const ImVec2& position, const char* begin, const char* end,
    SyntaxLanguage language, bool& inBlockComment) {
    Cursor cursor{drawList, position};
    highlight(&cursor, begin, end, language, inBlockComment);
}

void advanceBlockComment(const char* begin, const char* end, SyntaxLanguage language, bool& inBlockComment) {
    if (language != SyntaxLanguage::Glsl) {
        return;
    }
    highlight(nullptr, begin, end, language, inBlockComment);
}
