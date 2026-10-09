#include "editor/ScriptText.h"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace ScriptText;

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Применяет BlockEdit к тексту так же, как это сделает редактор.
std::string applied(const std::string& text, const BlockEdit& edit) {
    return text.substr(0, static_cast<std::size_t>(edit.begin)) + edit.replacement + text.substr(static_cast<std::size_t>(edit.end));
}

void diskFormatRoundTrips() {
    DiskFormat format;
    const std::string unix = "local a = 1\n  b = 2\n";
    require(fromDisk(unix, format) == unix && !format.crlf && !format.tabIndent && !format.bom, "plain LF file is untouched");
    require(toDisk(unix, format) == unix, "LF round trip");

    const std::string windows = "a = 1\r\nb = 2\r\n";
    require(fromDisk(windows, format) == "a = 1\nb = 2\n" && format.crlf, "CRLF is normalized and remembered");
    require(toDisk("a = 1\nb = 2\nc = 3\n", format) == "a = 1\r\nb = 2\r\nc = 3\r\n", "CRLF is restored on save");
    require(toDisk(fromDisk(windows, format), format) == windows, "CRLF round trip");

    const std::string bom = "\xEF\xBB\xBFprint(1)\n";
    require(fromDisk(bom, format) == "print(1)\n" && format.bom, "BOM is stripped and remembered");
    require(toDisk("print(1)\n", format) == bom, "BOM is restored on save");

    const std::string tabs = "function f()\n\tif x then\n\t\treturn 1\n\tend\nend\n";
    const std::string expanded = fromDisk(tabs, format);
    require(expanded == "function f()\n    if x then\n        return 1\n    end\nend\n" && format.tabIndent, "tab indentation becomes spaces");
    require(toDisk(expanded, format) == tabs, "tab indentation round trip");

    const std::string noTrailing = "a = 1";
    require(toDisk(fromDisk(noTrailing, format), format) == noTrailing, "no trailing newline stays so");
    require(fromDisk("", format).empty() && toDisk("", format).empty(), "empty file");
    require(fromDisk("x = 'привет'\n", format) == "x = 'привет'\n", "UTF-8 passes through");
    require(looksBinary(std::string("ab\0c", 4)) && !looksBinary("abc"), "binary detection");
}

void locatesOffsets() {
    const std::string text = "ab\nпривет\n\nend";
    int line = 0;
    int column = 0;
    locate(text, 0, line, column);
    require(line == 0 && column == 0, "start of text");
    locate(text, 2, line, column);
    require(line == 0 && column == 2, "end of the first line");
    locate(text, 3 + 6, line, column); // после «при» (по 2 байта на букву)
    require(line == 1 && column == 3, "column counts UTF-8 characters, not bytes");
    locate(text, static_cast<int>(text.size()), line, column);
    require(line == 3 && column == 3, "end of text");
    require(offsetOfLine(text, 0) == 0 && offsetOfLine(text, 1) == 3, "offset of a line");
    require(offsetOfLine(text, 99) == static_cast<int>(text.size()), "line past the end clamps to the size");
    require(countLines(text) == 4 && countLines("") == 1, "line count");
    require(lineStart(text, 5) == 3 && lineEnd(text, 5) == 3 + 12, "line bounds");
}

void parsesLuaErrors() {
    std::string path;
    int line = 0;
    require(parseErrorLocation("assets/scripts/enemy.lua:12: unexpected symbol near 'x'", path, line) &&
            path == "assets/scripts/enemy.lua" && line == 12, "syntax error location");
    require(errorText("assets/scripts/enemy.lua:12: unexpected symbol near 'x'") == "unexpected symbol near 'x'", "message without the location");
    require(parseErrorLocation("Not reloaded, the previous code stays active. assets/scripts/waves.lua:7: boom", path, line) &&
            path == "assets/scripts/waves.lua" && line == 7, "location after a prefix");
    require(errorText("Not reloaded. assets/scripts/waves.lua:7: boom\nstack traceback:\n\t[C]: in ?") == "boom", "first line of the message only");
    require(parseErrorLocation("enemy.lua:3: x", path, line) && path == "enemy.lua" && line == 3, "bare file name");
    require(!parseErrorLocation("cannot read file", path, line), "no location");
    require(!parseErrorLocation("file.lua: no digits", path, line), "colon without a line number");
    require(errorText("plain message") == "plain message", "message without a location is returned as is");
}

void insertsTabStops() {
    require(tabSpaces("", 0) == "    ", "tab at the start of a line");
    require(tabSpaces("ab", 2) == "  ", "tab after two characters reaches the next stop");
    require(tabSpaces("abcd", 4) == "    ", "tab on a stop is a full step");
    require(tabSpaces("x\nab", 4) == "  ", "column is counted from the line start");
}

void indentsNewLines() {
    // Отступ для новой строки после «body\n».
    auto after = [](const std::string& body) {
        const std::string text = body + "\n";
        return newLineIndent(text, static_cast<int>(text.size()));
    };
    require(after("a = 1").empty(), "plain line keeps no indent");
    require(after("  a = 1") == "  ", "indent of the previous line is kept");
    require(after("if x then") == "    ", "then opens a block");
    require(after("  for i = 1, 3 do") == "      ", "do opens a block inside an indent");
    require(after("else") == "    ", "else opens a block");
    require(after("repeat") == "    ", "repeat opens a block");
    require(after("local t = {") == "    ", "brace opens a block");
    require(after("function M:update(dt)") == "    ", "function header opens a block");
    require(after("local f = function(a, b)") == "    ", "anonymous function header opens a block");
    require(after("table.sort(t, function(a, b) return a < b end)").empty(), "one-line function does not");
    require(after("if x then -- check it") == "    ", "trailing comment is ignored");
    require(after("print('then') -- do").empty(), "keywords in strings and comments do not open a block");
    require(after("print('--') then") == "    ", "-- inside a string is not a comment");
    require(after("done").empty(), "words that only end like a keyword do not open a block");
    require(after("    -- end of the block").empty() == false, "a comment line keeps its indent");
    require(newLineIndent("x = 1", 5).empty(), "not after a newline");
    require(newLineIndent("a\nb\n", 2) == "", "cursor right after the first newline looks at line a");
}

void indentsBlocks() {
    const std::string text = "a\nb\nc\n";
    BlockEdit edit = indentLines(text, 0, 0, false);
    require(applied(text, edit) == "    a\nb\nc\n", "indent the cursor line");

    // Выделены b и c целиком.
    edit = indentLines(text, 2, 5, false);
    require(applied(text, edit) == "a\n    b\n    c\n", "indent selected lines");
    require(edit.selStart == 2 && edit.selEnd == 5 + 8, "selection keeps covering the lines");

    // Выделение кончается в начале строки c — её не трогаем.
    edit = indentLines(text, 0, 4, false);
    require(applied(text, edit) == "    a\n    b\nc\n", "a selection ending at a line start skips that line");

    const std::string indented = "    a\n  b\n\tc\n";
    edit = indentLines(indented, 0, static_cast<int>(indented.size()) - 1, true);
    require(applied(indented, edit) == "a\nb\n\tc\n", "outdent removes up to one step of spaces");

    edit = indentLines("  a\n", 3, 3, true);
    require(applied("  a\n", edit) == "a\n" && edit.selStart == 1, "outdent moves the cursor with the text");
    edit = indentLines("a\n", 1, 1, true);
    require(!edit.changed, "nothing to outdent");

    const std::string blank = "a\n\nb";
    edit = indentLines(blank, 0, static_cast<int>(blank.size()), false);
    require(applied(blank, edit) == "    a\n\n    b", "blank lines are not padded");
}
}

int main() {
    try {
        diskFormatRoundTrips();
        locatesOffsets();
        parsesLuaErrors();
        insertsTabStops();
        indentsNewLines();
        indentsBlocks();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "ScriptTextTests passed\n";
    return 0;
}
