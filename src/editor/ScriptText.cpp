#include "editor/ScriptText.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

namespace ScriptText {
namespace {
bool isIdentifierChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool isContinuationByte(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

int clampOffset(const std::string& text, int offset) {
    return std::clamp(offset, 0, static_cast<int>(text.size()));
}

// Строка без «-- комментария» в конце; кавычки учитываются, чтобы не обрезать "--" внутри строки.
std::string withoutComment(const std::string& line) {
    char quote = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quote) {
            if (c == '\\') {
                ++i;
            } else if (c == quote) {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '-' && i + 1 < line.size() && line[i + 1] == '-') {
            return line.substr(0, i);
        }
    }
    return line;
}

bool hasWord(const std::string& code, const std::string& word) {
    for (std::size_t pos = code.find(word); pos != std::string::npos; pos = code.find(word, pos + 1)) {
        const bool leftOk = pos == 0 || !isIdentifierChar(code[pos - 1]);
        const std::size_t after = pos + word.size();
        const bool rightOk = after >= code.size() || !isIdentifierChar(code[after]);
        if (leftOk && rightOk) {
            return true;
        }
    }
    return false;
}

// Строка открывает блок, если кончается на then / do / else / repeat, на { или (, либо это заголовок function(...) без end.
bool opensBlock(const std::string& code) {
    if (code.empty()) {
        return false;
    }
    if (code.back() == '{' || code.back() == '(') {
        return true;
    }
    std::size_t wordStart = code.size();
    while (wordStart > 0 && isIdentifierChar(code[wordStart - 1])) {
        --wordStart;
    }
    const std::string last = code.substr(wordStart);
    if (last == "then" || last == "do" || last == "else" || last == "repeat") {
        return true;
    }
    return code.back() == ')' && hasWord(code, "function") && !hasWord(code, "end");
}
} // namespace

std::string fromDisk(const std::string& bytes, DiskFormat& format) {
    format = {};
    std::size_t start = 0;
    if (bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        format.bom = true;
        start = 3;
    }
    std::size_t lineFeeds = 0;
    std::size_t crLineFeeds = 0;
    for (std::size_t i = start; i < bytes.size(); ++i) {
        if (bytes[i] == '\n') {
            ++lineFeeds;
            crLineFeeds += (i > start && bytes[i - 1] == '\r') ? 1 : 0;
        }
    }
    format.crlf = crLineFeeds > 0 && crLineFeeds * 2 >= lineFeeds;

    std::string text;
    text.reserve(bytes.size());
    int column = 0;
    bool inIndent = true;
    for (std::size_t i = start; i < bytes.size(); ++i) {
        char c = bytes[i];
        if (c == '\r') {
            if (i + 1 < bytes.size() && bytes[i + 1] == '\n') {
                continue;
            }
            c = '\n'; // одиночный CR — старый перевод строки
        }
        if (c == '\n') {
            text += '\n';
            column = 0;
            inIndent = true;
        } else if (c == '\t') {
            const int spaces = kTabWidth - column % kTabWidth;
            text.append(static_cast<std::size_t>(spaces), ' ');
            column += spaces;
            format.tabIndent = format.tabIndent || inIndent;
        } else {
            if (c != ' ') {
                inIndent = false;
            }
            text += c;
            if (!isContinuationByte(c)) {
                ++column;
            }
        }
    }
    return text;
}

std::string toDisk(const std::string& text, const DiskFormat& format) {
    std::string out;
    out.reserve(text.size() + 16);
    if (format.bom) {
        out += "\xEF\xBB\xBF";
    }
    std::size_t start = 0;
    while (true) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::size_t body = start;
        if (format.tabIndent) {
            std::size_t spaces = 0;
            while (start + spaces < end && text[start + spaces] == ' ') {
                ++spaces;
            }
            out.append(spaces / kTabWidth, '\t');
            out.append(spaces % kTabWidth, ' ');
            body = start + spaces;
        }
        out.append(text, body, end - body);
        if (end >= text.size()) {
            break;
        }
        out += format.crlf ? "\r\n" : "\n";
        start = end + 1;
    }
    return out;
}

bool looksBinary(const std::string& bytes) {
    return bytes.find('\0') != std::string::npos;
}

int lineStart(const std::string& text, int offset) {
    offset = clampOffset(text, offset);
    if (offset == 0) {
        return 0;
    }
    const std::size_t pos = text.rfind('\n', static_cast<std::size_t>(offset - 1));
    return pos == std::string::npos ? 0 : static_cast<int>(pos) + 1;
}

int lineEnd(const std::string& text, int offset) {
    offset = clampOffset(text, offset);
    const std::size_t pos = text.find('\n', static_cast<std::size_t>(offset));
    return pos == std::string::npos ? static_cast<int>(text.size()) : static_cast<int>(pos);
}

void locate(const std::string& text, int offset, int& line, int& column) {
    offset = clampOffset(text, offset);
    line = static_cast<int>(std::count(text.begin(), text.begin() + offset, '\n'));
    column = 0;
    for (int i = lineStart(text, offset); i < offset; ++i) {
        if (!isContinuationByte(text[static_cast<std::size_t>(i)])) {
            ++column;
        }
    }
}

int offsetOfLine(const std::string& text, int line) {
    int offset = 0;
    for (int current = 0; current < line; ++current) {
        const std::size_t pos = text.find('\n', static_cast<std::size_t>(offset));
        if (pos == std::string::npos) {
            return static_cast<int>(text.size());
        }
        offset = static_cast<int>(pos) + 1;
    }
    return offset;
}

int countLines(const std::string& text) {
    return static_cast<int>(std::count(text.begin(), text.end(), '\n')) + 1;
}

bool parseErrorLocation(const std::string& message, std::string& path, int& line) {
    std::size_t search = 0;
    for (std::size_t pos = message.find(".lua:"); pos != std::string::npos; pos = message.find(".lua:", search)) {
        search = pos + 5;
        std::size_t digitsEnd = search;
        while (digitsEnd < message.size() && std::isdigit(static_cast<unsigned char>(message[digitsEnd]))) {
            ++digitsEnd;
        }
        if (digitsEnd == search) {
            continue;
        }
        std::size_t begin = pos;
        while (begin > 0 && (isIdentifierChar(message[begin - 1]) || message[begin - 1] == '/' || message[begin - 1] == '.' ||
                                message[begin - 1] == '-')) {
            --begin;
        }
        const int number = std::atoi(message.substr(search, digitsEnd - search).c_str());
        if (number <= 0) {
            continue;
        }
        path = message.substr(begin, pos + 4 - begin);
        line = number;
        return true;
    }
    return false;
}

std::string errorText(const std::string& message) {
    std::string path;
    int line = 0;
    std::string rest = message;
    if (parseErrorLocation(message, path, line)) {
        const std::size_t marker = message.find(".lua:" + std::to_string(line));
        std::size_t after = marker + 5 + std::to_string(line).size();
        if (after < message.size() && message[after] == ':') {
            ++after;
        }
        while (after < message.size() && message[after] == ' ') {
            ++after;
        }
        rest = message.substr(after);
    }
    const std::size_t newline = rest.find('\n');
    return newline == std::string::npos ? rest : rest.substr(0, newline);
}

std::string tabSpaces(const std::string& text, int cursor) {
    int line = 0;
    int column = 0;
    locate(text, cursor, line, column);
    return std::string(static_cast<std::size_t>(kTabWidth - column % kTabWidth), ' ');
}

std::string newLineIndent(const std::string& text, int cursor) {
    if (cursor <= 0 || cursor > static_cast<int>(text.size()) || text[static_cast<std::size_t>(cursor - 1)] != '\n') {
        return {};
    }
    const int previousEnd = cursor - 1;
    const int previousStart = lineStart(text, previousEnd);
    const std::string line = text.substr(static_cast<std::size_t>(previousStart), static_cast<std::size_t>(previousEnd - previousStart));
    std::size_t spaces = line.find_first_not_of(' ');
    if (spaces == std::string::npos) {
        spaces = line.size();
    }
    std::string indent(spaces, ' ');
    std::string code = withoutComment(line);
    while (!code.empty() && std::isspace(static_cast<unsigned char>(code.back()))) {
        code.pop_back();
    }
    if (opensBlock(code)) {
        indent.append(static_cast<std::size_t>(kTabWidth), ' ');
    }
    return indent;
}

BlockEdit indentLines(const std::string& text, int selStart, int selEnd, bool outdent) {
    selStart = clampOffset(text, selStart);
    selEnd = clampOffset(text, selEnd);
    if (selEnd < selStart) {
        std::swap(selStart, selEnd);
    }
    BlockEdit edit;
    edit.begin = lineStart(text, selStart);
    // Выделение, оканчивающееся в самом начале строки, эту строку не задевает.
    const int lastOffset = (selEnd > selStart && text[static_cast<std::size_t>(selEnd - 1)] == '\n') ? selEnd - 1 : selEnd;
    edit.end = lineEnd(text, lastOffset);

    struct Line {
        int oldStart;
        int newStart; // от начала диапазона
        int delta;
    };
    std::vector<Line> lines;
    int position = edit.begin;
    while (true) {
        const int end = lineEnd(text, position);
        const int length = end - position;
        int delta = 0;
        lines.push_back({position, static_cast<int>(edit.replacement.size()), 0});
        if (outdent) {
            int spaces = 0;
            while (spaces < kTabWidth && spaces < length && text[static_cast<std::size_t>(position + spaces)] == ' ') {
                ++spaces;
            }
            delta = -spaces;
            edit.replacement.append(text, static_cast<std::size_t>(position + spaces), static_cast<std::size_t>(length - spaces));
        } else {
            if (length > 0) {
                edit.replacement.append(static_cast<std::size_t>(kTabWidth), ' ');
                delta = kTabWidth;
            }
            edit.replacement.append(text, static_cast<std::size_t>(position), static_cast<std::size_t>(length));
        }
        lines.back().delta = delta;
        if (end >= edit.end) {
            break;
        }
        edit.replacement += '\n';
        position = end + 1;
    }

    const int totalShift = static_cast<int>(edit.replacement.size()) - (edit.end - edit.begin);
    auto map = [&](int offset) {
        if (offset > edit.end) {
            return offset + totalShift;
        }
        const Line* line = &lines.front();
        for (const Line& candidate : lines) {
            if (candidate.oldStart <= offset) {
                line = &candidate;
            }
        }
        const int column = offset - line->oldStart;
        const int shifted = column == 0 ? 0 : std::max(0, column + line->delta);
        return edit.begin + line->newStart + shifted;
    };
    edit.selStart = map(selStart);
    edit.selEnd = map(selEnd);
    edit.changed = edit.replacement != text.substr(static_cast<std::size_t>(edit.begin), static_cast<std::size_t>(edit.end - edit.begin));
    return edit;
}
} // namespace ScriptText
