#pragma once

#include <string>

// Текстовая модель Lua-скрипта в окне редактора, без ImGui — чтобы её можно было тестировать отдельно.
// В буфере редактора текст всегда с '\n', без BOM и без табов: ImGui не показывает '\r' и не знает ширину таба.
// Как файл лежал на диске (CRLF, отступы табами, BOM), запоминается и возвращается при сохранении.
// Используют ScriptEditorPanel (редактор, автоотступы, номера строк в ошибках) и GameplayPanel (разбор сообщений Lua).
// Проверяются тестом tests/ScriptTextTests.cpp.
namespace ScriptText {
// Ширина таба в колонках и шаг отступа: Tab ставит пробелы до ближайшей границы в 4 колонки.
constexpr int kTabWidth = 4;

// Как файл лежал на диске: crlf (переводы строк были \r\n), tabIndent (отступы в начале строк были табами),
// bom (в начале файла стоял BOM UTF-8). Эти признаки возвращаются при сохранении, чтобы редактор не менял формат чужого файла.
struct DiskFormat {
    bool crlf = false;
    bool tabIndent = false;
    bool bom = false;
};

// Байты файла → текст буфера.
// Убирает BOM, превращает CRLF и одиночный CR в '\n', раскрывает табы в пробелы по границам kTabWidth.
// Что из этого было в файле, записывает в format.
std::string fromDisk(const std::string& bytes, DiskFormat& format);
// Текст буфера → байты файла в прежнем формате.
// Если был tabIndent, ведущие пробелы собираются обратно в табы (по kTabWidth пробелов на таб).
// Табы в середине строки не восстанавливаются: после fromDisk они остаются пробелами.
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
// Путь это подряд идущие буквы, цифры и символы _ - . / перед «.lua:». Число после двоеточия должно быть больше нуля.
bool parseErrorLocation(const std::string& message, std::string& path, int& line);
// То же сообщение без «путь:строка:» впереди — для показа рядом с номером строки.
// Берётся только первая строка сообщения: дальше у Lua обычно идёт traceback.
std::string errorText(const std::string& message);

// Tab без выделения: пробелы до следующей позиции табуляции.
std::string tabSpaces(const std::string& text, int cursor);
// Отступ новой строки после Enter. cursor стоит сразу за только что вставленным '\n':
// берём отступ предыдущей строки и добавляем шаг после then / do / else / repeat / { / function(...).
std::string newLineIndent(const std::string& text, int cursor);

// Замена диапазона [begin, end) текста и новое выделение.
// changed равно false, если замена ничего не меняет (например, Shift+Tab у строк без отступа).
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
