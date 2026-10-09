#pragma once

#include <string>

// Текстовая модель Lua-скрипта в окне редактора, без ImGui — чтобы её можно было тестировать отдельно.
// В буфере редактора текст всегда с '\n', без BOM и без табов: ImGui не показывает '\r' и не знает ширину таба.
// Как файл лежал на диске (CRLF, отступы табами, BOM), запоминается и возвращается при сохранении.
namespace ScriptText {
constexpr int kTabWidth = 4;

struct DiskFormat {
    bool crlf = false;
    bool tabIndent = false;
    bool bom = false;
};

// Байты файла → текст буфера.
std::string fromDisk(const std::string& bytes, DiskFormat& format);
// Текст буфера → байты файла в прежнем формате.
std::string toDisk(const std::string& text, const DiskFormat& format);
// Нулевой байт — это не текст: такой файл в редакторе не открываем.
bool looksBinary(const std::string& bytes);

// Начало и конец (до '\n') строки, в которой стоит offset.
int lineStart(const std::string& text, int offset);
int lineEnd(const std::string& text, int offset);
// Строка и колонка (обе с 0, колонка в символах UTF-8) для смещения в байтах.
void locate(const std::string& text, int offset, int& line, int& column);
// Смещение начала строки line (с 0); для строки за концом текста — размер текста.
int offsetOfLine(const std::string& text, int line);
int countLines(const std::string& text);

// «assets/scripts/enemy.lua:12: unexpected symbol» → путь и строка (с 1). Перед путём может быть любой текст.
bool parseErrorLocation(const std::string& message, std::string& path, int& line);
// То же сообщение без «путь:строка:» впереди — для показа рядом с номером строки.
std::string errorText(const std::string& message);

// Tab без выделения: пробелы до следующей позиции табуляции.
std::string tabSpaces(const std::string& text, int cursor);
// Отступ новой строки после Enter. cursor стоит сразу за только что вставленным '\n':
// берём отступ предыдущей строки и добавляем шаг после then / do / else / repeat / { / function(...).
std::string newLineIndent(const std::string& text, int cursor);

// Замена диапазона [begin, end) текста и новое выделение.
struct BlockEdit {
    bool changed = false;
    int begin = 0;
    int end = 0;
    std::string replacement;
    int selStart = 0;
    int selEnd = 0;
};
// Tab / Shift+Tab над строками, которых касается выделение [selStart, selEnd] (курсор без выделения — одна строка).
BlockEdit indentLines(const std::string& text, int selStart, int selEnd, bool outdent);
} // namespace ScriptText
