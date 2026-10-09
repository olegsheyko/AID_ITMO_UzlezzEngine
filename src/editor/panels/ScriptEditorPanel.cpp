#include "editor/panels/ScriptEditorPanel.h"

#include "core/Logger.h"
#include "editor/EditorContext.h"
#include "editor/EditorIcons.h"
#include "editor/EditorTheme.h"
#include "editor/EditorWidgets.h"
#include "editor/EditorWindows.h"
#include "editor/IconsLucide.h"
#include "editor/PlatformShell.h"
#include "editor/SyntaxHighlight.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>

using namespace EditorTheme;
using EditorUI::px;

namespace {
namespace fs = std::filesystem;

// Больше — это уже не скрипт: поле ввода каждый кадр разбирает весь буфер.
constexpr std::uintmax_t kMaxEditableBytes = 1024 * 1024;
constexpr double kDiskPollSeconds = 0.5;
constexpr double kValidateDelaySeconds = 0.35;
// Сколько секунд после сохранения показываем ответ hot reload вместо «Saved».
constexpr double kReloadResultSeconds = 8.0;
constexpr const char* kCloseDialog = "Unsaved changes##script_close";

constexpr ImU32 kEditorBackground = IM_COL32(23, 23, 25, 255);
constexpr ImU32 kGutterBackground = IM_COL32(28, 28, 30, 255);
constexpr ImU32 kCurrentLine = IM_COL32(255, 255, 255, 10);
constexpr ImU32 kErrorLineTint = IM_COL32(240, 86, 78, 38);
constexpr ImU32 kLineNumber = IM_COL32(82, 82, 90, 255);
constexpr ImU32 kLineNumberCurrent = IM_COL32(170, 170, 178, 255);

std::string normalizePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

std::string fileName(const std::string& path) {
    return fs::path(path).filename().string();
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

std::string clockText() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[16] = {};
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
    return buffer;
}

// «enemy.lua» из сообщения об ошибке → полный путь в assets, если файл по этому пути не нашёлся.
std::string resolvePath(EditorContext& context, const std::string& request) {
    const std::string path = normalizePath(request);
    std::error_code error;
    if (fs::exists(path, error)) {
        return path;
    }
    std::string wanted = fileName(path);
    std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const AssetEntry& entry : context.assets.search(wanted, 50)) {
        std::string name = entry.name;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name == wanted) {
            return entry.path;
        }
    }
    return path;
}

// Плашка-сообщение над редактором. Возвращает дочернее окно открытым: содержимое рисует вызывающий, затем EndChild().
bool beginBanner(const char* id, ImU32 color) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toVec4(withAlpha(color, 0.12f)));
    ImGui::PushStyleColor(ImGuiCol_Border, toVec4(withAlpha(color, 0.45f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(10.0f), px(4.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
    const bool visible = ImGui::BeginChild(id, ImVec2(0.0f, ImGui::GetFrameHeight() + px(10.0f)),
        ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    return visible;
}

ImGuiWindow* findInputChild(ImGuiID inputId) {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiWindow* parent = ImGui::GetCurrentWindow();
    for (ImGuiWindow* window : g.Windows) {
        if (window->ChildId == inputId && window->ParentWindow == parent) {
            return window;
        }
    }
    return nullptr;
}
} // namespace

bool ScriptEditorPanel::isScript(const std::string& path) {
    std::string extension = fs::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".lua";
}

ScriptEditorPanel::Document* ScriptEditorPanel::find(const std::string& path) const {
    for (const auto& doc : documents_) {
        if (doc->path == path) {
            return doc.get();
        }
    }
    return nullptr;
}

ScriptEditorPanel::Document* ScriptEditorPanel::active() const {
    return activePath_.empty() ? nullptr : find(activePath_);
}

bool ScriptEditorPanel::openFile(const std::string& request, int line) {
    const std::string path = normalizePath(request);
    if (!isScript(path)) {
        return false;
    }
    Document* doc = find(path);
    if (!doc) {
        auto created = std::make_unique<Document>();
        created->path = path;
        load(*created);
        doc = created.get();
        documents_.push_back(std::move(created));
    }
    selectRequest_ = path;
    if (doc->loadError.empty()) {
        doc->focusRequest = true;
        if (line > 0) {
            gotoLine(*doc, line);
        }
    }
    return true;
}

bool ScriptEditorPanel::hasUnsavedChanges() const {
    return std::any_of(documents_.begin(), documents_.end(), [](const auto& doc) { return doc->loadError.empty() && doc->dirty(); });
}

std::vector<std::string> ScriptEditorPanel::unsavedFiles() const {
    std::vector<std::string> files;
    for (const auto& doc : documents_) {
        if (doc->loadError.empty() && doc->dirty()) {
            files.push_back(doc->path);
        }
    }
    return files;
}

bool ScriptEditorPanel::saveAll() {
    bool ok = true;
    for (const auto& doc : documents_) {
        if (!doc->loadError.empty() || !doc->dirty()) {
            continue;
        }
        // Файл, изменённый снаружи, молча не затираем: сначала пользователь выбирает версию.
        ok = !doc->conflict && save(*doc) && ok;
    }
    return ok;
}

void ScriptEditorPanel::discardAll() {
    documents_.clear();
    activePath_.clear();
    closeRequest_.clear();
}

void ScriptEditorPanel::load(Document& doc) {
    doc.loadError.clear();
    std::error_code error;
    const std::uintmax_t size = fs::file_size(doc.path, error);
    if (error) {
        doc.loadError = "Cannot open " + doc.path + ": the file does not exist or is not readable.";
        return;
    }
    if (size > kMaxEditableBytes) {
        doc.loadError = "This script is " + AssetDatabase::formatSize(size) + ", too large to edit in the editor. Open it in an external editor.";
        return;
    }
    std::string bytes;
    if (!readFile(doc.path, bytes)) {
        doc.loadError = "Cannot read " + doc.path + ".";
        return;
    }
    if (ScriptText::looksBinary(bytes)) {
        doc.loadError = "This file contains binary data, it is not a Lua script.";
        return;
    }
    doc.diskTime = fs::last_write_time(doc.path, error);
    doc.diskSize = size;
    doc.saved = ScriptText::fromDisk(bytes, doc.format);
    doc.text = doc.saved;
    doc.indexDirty = true;
    doc.validateDue = true;
    doc.resetInput = true;
    doc.missing = false;
    doc.conflict = false;
}

bool ScriptEditorPanel::save(Document& doc) {
    if (!doc.loadError.empty()) {
        return false;
    }
    const std::string bytes = ScriptText::toDisk(doc.text, doc.format);
    std::ofstream out(doc.path, std::ios::binary | std::ios::trunc);
    if (out) {
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
    }
    if (!out) {
        doc.status = "Could not write " + doc.path + ": check that the file is not read-only.";
        doc.statusIsError = true;
        LOG_ERROR("Script editor: " + doc.status);
        return false;
    }
    std::error_code error;
    doc.diskTime = fs::last_write_time(doc.path, error);
    doc.diskSize = bytes.size();
    doc.saved = doc.text;
    doc.missing = false;
    doc.conflict = false;
    doc.diskCopy.clear();
    doc.savedAt = ImGui::GetTime();
    doc.messageAtSave = lastScriptMessage_;
    validate(doc);
    doc.status = "Saved " + clockText();
    doc.statusIsError = false;
    if (!doc.error.empty()) {
        doc.status += ", with a syntax error: running scripts keep the previous code";
        doc.statusIsError = true;
    }
    LOG_INFO("Script editor: saved " + doc.path);
    return true;
}

void ScriptEditorPanel::revert(Document& doc) {
    replaceText(doc, doc.saved);
    doc.status = "Reverted to the saved version";
    doc.statusIsError = false;
}

void ScriptEditorPanel::replaceText(Document& doc, std::string text) {
    doc.text = std::move(text);
    doc.indexDirty = true;
    doc.validateDue = true;
    doc.resetInput = true;
    doc.pendingCursor = std::min(doc.cursor, static_cast<int>(doc.text.size()));
    doc.focusRequest = doc.focusRequest || doc.hadFocus;
}

void ScriptEditorPanel::rebuildIndex(Document& doc) {
    doc.lineStarts.clear();
    doc.lineInComment.clear();
    const char* text = doc.text.c_str();
    const char* end = text + doc.text.size();
    doc.lineStarts.push_back(0);
    for (const char* p = text; p < end; ++p) {
        if (*p == '\n') {
            doc.lineStarts.push_back(static_cast<int>(p - text) + 1);
        }
    }
    bool inComment = false;
    for (std::size_t i = 0; i < doc.lineStarts.size(); ++i) {
        doc.lineInComment.push_back(inComment ? 1 : 0);
        const char* lineEnd = i + 1 < doc.lineStarts.size() ? text + doc.lineStarts[i + 1] - 1 : end;
        advanceBlockComment(text + doc.lineStarts[i], lineEnd, SyntaxLanguage::Lua, inComment);
    }
    doc.indexDirty = false;
}

void ScriptEditorPanel::validate(Document& doc) {
    doc.validateDue = false;
    std::string error;
    if (ScriptWatcher::validate(doc.path, doc.text, error)) {
        doc.error.clear();
        doc.errorLine = 0;
        return;
    }
    doc.error = error.empty() ? doc.path + ": syntax error" : error;
    std::string path;
    int line = 0;
    doc.errorLine = ScriptText::parseErrorLocation(doc.error, path, line) ? line : 0;
}

void ScriptEditorPanel::checkDisk(Document& doc) {
    if (!doc.loadError.empty()) {
        return;
    }
    std::error_code error;
    if (!fs::exists(doc.path, error)) {
        doc.missing = true;
        return;
    }
    const fs::file_time_type time = fs::last_write_time(doc.path, error);
    const std::uintmax_t size = fs::file_size(doc.path, error);
    if (error) {
        return;
    }
    const bool reappeared = doc.missing;
    doc.missing = false;
    if (!reappeared && time == doc.diskTime && size == doc.diskSize) {
        return;
    }
    std::string bytes;
    if (size > kMaxEditableBytes || !readFile(doc.path, bytes) || ScriptText::looksBinary(bytes)) {
        return;
    }
    doc.diskTime = time;
    doc.diskSize = size;
    ScriptText::DiskFormat format;
    std::string disk = ScriptText::fromDisk(bytes, format);
    doc.format = format;
    if (disk == doc.saved && !reappeared) {
        return; // файл тронули, а содержимое то же
    }
    if (!doc.dirty()) {
        doc.saved = disk;
        replaceText(doc, std::move(disk));
        doc.status = "Reloaded: the file was changed outside the editor";
        doc.statusIsError = false;
    } else if (disk == doc.text) {
        doc.saved = std::move(disk);
        doc.conflict = false;
    } else {
        doc.conflict = true;
        doc.diskCopy = std::move(disk);
    }
}

void ScriptEditorPanel::gotoLine(Document& doc, int line) {
    doc.pendingCursor = ScriptText::offsetOfLine(doc.text, std::max(0, line - 1));
    doc.centerCursor = true;
    doc.focusRequest = true;
}

void ScriptEditorPanel::closeDocument(const std::string& path) {
    documents_.erase(std::remove_if(documents_.begin(), documents_.end(), [&path](const auto& doc) { return doc->path == path; }),
        documents_.end());
    if (activePath_ == path) {
        activePath_.clear();
    }
}

int ScriptEditorPanel::onText(ImGuiInputTextCallbackData* data) {
    Document& doc = *static_cast<Document*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        doc.text.resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = doc.text.data();
        return 0;
    }
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter) {
        return data->EventChar == '\t' ? 1 : 0; // Tab ставит пробелы сам редактор (ниже)
    }

    // CallbackEdit или CallbackAlways: правка уже в буфере, поле ещё не вернуло управление.
    if (doc.enterKey) {
        doc.enterKey = false;
        // Enter вставил ровно один '\n' — переносим отступ предыдущей строки.
        if (data->BufTextLen == static_cast<int>(doc.text.size()) + 1 && data->CursorPos > 0 && data->Buf[data->CursorPos - 1] == '\n') {
            const std::string current(data->Buf, static_cast<std::size_t>(data->BufTextLen));
            const std::string indent = ScriptText::newLineIndent(current, data->CursorPos);
            if (!indent.empty()) {
                data->InsertChars(data->CursorPos, indent.c_str(), indent.c_str() + indent.size());
                data->SelectionStart = data->SelectionEnd = data->CursorPos;
            }
        }
    }
    if (doc.tabKey) {
        doc.tabKey = false;
        const std::string current(data->Buf, static_cast<std::size_t>(data->BufTextLen));
        // Без выделения SelectionStart/End у ImGui — остатки прошлого выделения, а не курсор.
        const bool hasSelection = data->SelectionStart != data->SelectionEnd;
        const int from = hasSelection ? std::min(data->SelectionStart, data->SelectionEnd) : data->CursorPos;
        const int to = hasSelection ? std::max(data->SelectionStart, data->SelectionEnd) : data->CursorPos;
        const bool shift = ImGui::GetIO().KeyShift;
        const bool wholeLines = to > from && current.find('\n', static_cast<std::size_t>(from)) < static_cast<std::size_t>(to);
        if (shift || wholeLines) {
            const ScriptText::BlockEdit edit = ScriptText::indentLines(current, from, to, shift);
            if (edit.changed) {
                data->DeleteChars(edit.begin, edit.end - edit.begin);
                data->InsertChars(edit.begin, edit.replacement.c_str(), edit.replacement.c_str() + edit.replacement.size());
                data->SelectionStart = edit.selStart;
                data->SelectionEnd = edit.selEnd;
                data->CursorPos = edit.selEnd;
            }
        } else {
            if (to > from) {
                data->DeleteChars(from, to - from);
            }
            const std::string spaces = ScriptText::tabSpaces(std::string(data->Buf, static_cast<std::size_t>(data->BufTextLen)), from);
            data->InsertChars(from, spaces.c_str(), spaces.c_str() + spaces.size());
            data->SelectionStart = data->SelectionEnd = data->CursorPos;
        }
    }
    if (doc.pendingCursor >= 0) {
        const int position = std::min(doc.pendingCursor, data->BufTextLen);
        data->CursorPos = data->SelectionStart = data->SelectionEnd = position;
        doc.pendingCursor = -1;
        doc.centerCursor = false;
    }

    // Строка и колонка курсора для статус-бара и подсветки текущей строки.
    const char* buffer = data->Buf;
    const int cursor = std::clamp(data->CursorPos, 0, data->BufTextLen);
    int lineStart = cursor;
    while (lineStart > 0 && buffer[lineStart - 1] != '\n') {
        --lineStart;
    }
    int column = 0;
    for (int i = lineStart; i < cursor; ++i) {
        column += (static_cast<unsigned char>(buffer[i]) & 0xC0) != 0x80 ? 1 : 0;
    }
    const bool selected = data->SelectionStart != data->SelectionEnd;
    doc.cursor = cursor;
    doc.selStart = selected ? data->SelectionStart : cursor;
    doc.selEnd = selected ? data->SelectionEnd : cursor;
    doc.line = static_cast<int>(std::count(buffer, buffer + cursor, '\n'));
    doc.column = column;
    return 0;
}

void ScriptEditorPanel::draw(EditorContext& context) {
    // Сообщение скриптинга на начало кадра: сохранение в этом кадре отличит от него ответ hot reload.
    lastScriptMessage_ = context.scriptMessage;
    if (!context.openScriptRequest.empty()) {
        if (openFile(resolvePath(context, context.openScriptRequest), context.openScriptLine)) {
            open = true;
            context.focusWindowRequest = EditorWindow::kScriptEditor;
        }
        context.openScriptRequest.clear();
        context.openScriptLine = 0;
    }
    const double now = ImGui::GetTime();
    if (now - lastDiskCheck_ >= kDiskPollSeconds) {
        lastDiskCheck_ = now;
        for (const auto& doc : documents_) {
            checkDisk(*doc);
        }
    }
    if (!open) {
        return;
    }

    // Впервые окно садится вкладкой рядом со Scene, а не плавает отдельно; дальше его место хранит imgui.ini.
    if (const ImGuiWindow* scene = ImGui::FindWindowByName(EditorWindow::kScene); scene && scene->DockId != 0) {
        ImGui::SetNextWindowDockID(scene->DockId, ImGuiCond_FirstUseEver);
    }
    ImGui::SetNextWindowSize(ImVec2(px(760.0f), px(520.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(EditorWindow::kScriptEditor, &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End();
        return;
    }
    activePath_.clear();
    if (documents_.empty()) {
        drawEmpty(context);
    } else {
        const ImGuiTabBarFlags tabFlags = ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs |
            ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_TabListPopupButton;
        std::string closing;
        if (ImGui::BeginTabBar("##script_tabs", tabFlags)) {
            for (const auto& entry : documents_) {
                Document& doc = *entry;
                ImGuiTabItemFlags flags = doc.loadError.empty() && doc.dirty() ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
                if (selectRequest_ == doc.path) {
                    flags |= ImGuiTabItemFlags_SetSelected;
                }
                bool keepOpen = true;
                const std::string label = fileName(doc.path) + "###" + doc.path;
                const bool selected = ImGui::BeginTabItem(label.c_str(), &keepOpen, flags);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                    ImGui::SetTooltip("%s", PlatformShell::absolutePath(doc.path).c_str());
                }
                if (selected) {
                    activePath_ = doc.path;
                    drawDocument(context, doc);
                    ImGui::EndTabItem();
                }
                if (!keepOpen) {
                    closing = doc.path;
                }
            }
            ImGui::EndTabBar();
        }
        selectRequest_.clear();
        if (!closing.empty()) {
            const Document* doc = find(closing);
            if (doc && doc->loadError.empty() && doc->dirty()) {
                closeRequest_ = closing;
                ImGui::OpenPopup(kCloseDialog);
            } else {
                closeDocument(closing);
            }
        }
    }
    drawCloseDialog();

    // Скрипт, брошенный из Content Browser на окно, открывается во вкладке.
    if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->Rect(), ImGui::GetID("##script_drop"))) {
        const ImGuiPayload* peek = ImGui::GetDragDropPayload();
        if (peek && peek->IsDataType(kAssetPayload) && isScript(static_cast<const char*>(peek->Data))) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetPayload)) {
                openFile(static_cast<const char*>(payload->Data));
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::End();
}

void ScriptEditorPanel::drawEmpty(EditorContext& context) {
    const float top = std::max(px(130.0f), ImGui::GetContentRegionAvail().y * 0.34f);
    if (ImGui::BeginChild("##script_empty", ImVec2(0.0f, top), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar)) {
        EditorUI::emptyState(ICON_LC_FILE_CODE, "No script open",
            "Double-click a .lua file in the Content Browser, press Edit in the Inspector, or pick one below.");
    }
    ImGui::EndChild();
    EditorUI::pushSemibold();
    ImGui::TextUnformatted("Scripts in the project");
    EditorUI::popFont();
    int shown = 0;
    for (const AssetEntry& entry : context.assets.search(".lua", 200)) {
        if (entry.extension != ".lua") {
            continue;
        }
        ++shown;
        const std::string label = std::string(ICON_LC_FILE_CODE "  ") + entry.path;
        if (ImGui::Selectable(label.c_str())) {
            openFile(entry.path);
        }
    }
    if (shown == 0) {
        EditorUI::textFaint("No .lua files in assets.");
    }
}

void ScriptEditorPanel::drawDocument(EditorContext& context, Document& doc) {
    ImGui::PushID(doc.path.c_str());
    drawToolbar(context, doc);
    if (!doc.loadError.empty()) {
        ImGui::Dummy(ImVec2(0.0f, px(4.0f)));
        ImGui::TextColored(toVec4(kWarning), ICON_LC_CIRCLE_ALERT);
        ImGui::SameLine();
        ImGui::TextWrapped("%s", doc.loadError.c_str());
        ImGui::PopID();
        return;
    }
    drawBanners(doc);
    const float statusHeight = ImGui::GetFrameHeight();
    const float height = ImGui::GetContentRegionAvail().y - statusHeight - ImGui::GetStyle().ItemSpacing.y;
    drawEditor(doc, std::max(height, px(80.0f)));
    drawStatusBar(context, doc);
    ImGui::PopID();
}

void ScriptEditorPanel::drawToolbar(EditorContext& context, Document& doc) {
    const bool editable = doc.loadError.empty();
    const bool canSave = editable && doc.dirty() && !doc.conflict;
    const std::string saveTip = "Save  " + EditorUI::shortcut("Ctrl+S") + "\nSaved scripts reload in the running game when \"Reload on save\" is on.";
    ImGui::BeginDisabled(!canSave);
    if (EditorUI::toolButton("##save", ICON_LC_SAVE, "Save", canSave, saveTip.c_str())) {
        save(doc);
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::BeginDisabled(!editable || !doc.dirty());
    if (EditorUI::toolButton("##revert", ICON_LC_ROTATE_CCW, "Revert", false, "Discard unsaved changes and go back to the saved version")) {
        revert(doc);
    }
    ImGui::EndDisabled();
    EditorUI::toolbarSeparator();
    if (EditorUI::toolButton("##external", ICON_LC_EXTERNAL_LINK, "Open Externally", false,
            "Open the file in the default app for .lua files.\nIt opens the saved version: save first to send your latest changes.")) {
        PlatformShell::openFile(doc.path);
    }
    ImGui::SameLine(0.0f, 2.0f);
    const std::string reveal = "Show in " + PlatformShell::fileManagerName();
    if (EditorUI::toolButton("##reveal", ICON_LC_FOLDER_OPEN, nullptr, false, reveal.c_str())) {
        PlatformShell::revealInFileManager(doc.path);
    }
    EditorUI::toolbarSeparator();
    ImGui::AlignTextToFramePadding();
    EditorUI::checkbox("Reload on save##script_auto", &context.autoReloadScripts);
    ImGui::SetItemTooltip("Same switch as in the Gameplay panel: a saved .lua file is checked and applied to the running game,\n"
                          "in Edit and in Play. A broken script is not applied, the previous code keeps running.");
    if (!context.autoReloadScripts) {
        ImGui::SameLine();
        if (EditorUI::toolButton("##reload_now", ICON_LC_REFRESH_CW, "Reload Scripts", false, "Apply the saved scripts to the running game now")) {
            context.reloadScripts();
        }
    }
}

void ScriptEditorPanel::drawBanners(Document& doc) {
    if (doc.conflict) {
        if (beginBanner("##banner_conflict", kWarning)) {
            const float rowEnd = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(toVec4(kWarning), ICON_LC_TRIANGLE_ALERT);
            ImGui::SameLine();
            ImGui::TextUnformatted("Changed on disk while you were editing.");
            const float buttons = ImGui::CalcTextSize("Load from Disk").x + ImGui::CalcTextSize("Keep Mine").x + ImGui::GetStyle().FramePadding.x * 4.0f +
                ImGui::GetStyle().ItemSpacing.x;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(8.0f), rowEnd - buttons));
            if (ImGui::SmallButton("Load from Disk")) {
                const std::string disk = doc.diskCopy;
                doc.saved = disk;
                replaceText(doc, disk);
                doc.conflict = false;
                doc.diskCopy.clear();
                doc.status = "Loaded the version from disk";
                doc.statusIsError = false;
            }
            ImGui::SetItemTooltip("Drop your unsaved changes and take the file as it is on disk.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Keep Mine")) {
                doc.saved = doc.diskCopy;
                doc.conflict = false;
                doc.diskCopy.clear();
            }
            ImGui::SetItemTooltip("Keep what is in the editor; the next Save overwrites the file on disk.");
        }
        ImGui::EndChild();
    }
    if (doc.missing) {
        if (beginBanner("##banner_missing", kWarning)) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(toVec4(kWarning), ICON_LC_TRIANGLE_ALERT);
            ImGui::SameLine();
            ImGui::TextUnformatted("The file was deleted or moved. Save to create it again.");
        }
        ImGui::EndChild();
    }
    if (!doc.error.empty()) {
        const std::string text = doc.errorLine > 0 ? "Line " + std::to_string(doc.errorLine) + ": " + ScriptText::errorText(doc.error)
                                                    : ScriptText::errorText(doc.error);
        if (beginBanner("##banner_error", kError)) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(toVec4(kError), ICON_LC_CIRCLE_ALERT);
            ImGui::SameLine();
            ImGui::TextUnformatted(EditorUI::ellipsize(text.c_str(), ImGui::GetContentRegionAvail().x).c_str());
        }
        ImGui::EndChild();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            ImGui::SetTooltip("%s\nClick to jump to the line", doc.error.c_str());
        }
        if (doc.errorLine > 0 && ImGui::IsItemClicked()) {
            gotoLine(doc, doc.errorLine);
        }
    }
}

void ScriptEditorPanel::drawEditor(Document& doc, float height) {
    if (doc.indexDirty) {
        rebuildIndex(doc);
    }
    if (doc.validateDue && ImGui::GetTime() - doc.editedAt >= kValidateDelaySeconds) {
        validate(doc);
    }

    EditorUI::pushMono();
    const float lineHeight = ImGui::GetFontSize();
    const float charWidth = ImGui::CalcTextSize("0").x;
    int digits = 3;
    for (int count = static_cast<int>(doc.lineStarts.size()); count >= 1000; count /= 10) {
        ++digits;
    }
    // Колонка номеров строк — это левый отступ текста внутри поля: поле само считает по нему клики и прокрутку.
    const float gutterWidth = std::ceil(charWidth * static_cast<float>(digits)) + px(18.0f);
    const float paddingX = gutterWidth + px(10.0f);
    const float paddingY = px(6.0f);

    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiID id = ImGui::GetID("##source");
    const bool wasActive = ImGui::GetActiveID() == id;
    if (doc.resetInput) {
        // Текст подменили снаружи (Revert, перечитывание с диска): поле держит свою копию, пока активно,
        // и при потере фокуса вернуло бы её в буфер поверх нашего текста.
        if (wasActive) {
            ImGui::ClearActiveID();
        }
        if (g.InputTextDeactivatedState.ID == id) {
            g.InputTextDeactivatedState.ID = 0;
        }
        doc.resetInput = false;
    }
    if (wasActive) {
        // Tab и Shift+Tab — отступы, а не переход по виджетам: без этого Shift+Tab уводит фокус из поля.
        ImGui::SetKeyOwner(ImGuiKey_Tab, id);
    }
    doc.tabKey = wasActive && ImGui::IsKeyPressed(ImGuiKey_Tab) && !io.KeyCtrl && !io.KeyAlt;
    doc.enterKey = wasActive && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) && !io.KeyCtrl && !io.KeyAlt;

    // Вставка с табами: поле не умеет их показывать, поэтому на время вставки буфер обмена отдаёт пробелы.
    std::string clipboardBackup;
    bool clipboardSwapped = false;
    if (wasActive && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
        if (const char* clipboard = ImGui::GetClipboardText(); clipboard && std::strchr(clipboard, '\t')) {
            clipboardBackup = clipboard;
            ScriptText::DiskFormat ignored;
            ImGui::SetClipboardText(ScriptText::fromDisk(clipboardBackup, ignored).c_str());
            clipboardSwapped = true;
        }
    }
    if (doc.focusRequest) {
        ImGui::SetKeyboardFocusHere();
        doc.focusRequest = false;
    }
    if (doc.centerCursor && doc.pendingCursor >= 0 && wasActive) {
        if (ImGuiInputTextState* state = ImGui::GetInputTextState(id)) {
            state->CursorCenterY = true;
        }
    }

    ImGui::PushStyleColor(ImGuiCol_FrameBg, toVec4(kEditorBackground));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)); // текст рисуем сами, с подсветкой
    ImGui::PushStyleColor(ImGuiCol_InputTextCursor, toVec4(kText));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(paddingX, paddingY));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize |
        ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackEdit | ImGuiInputTextFlags_CallbackAlways;
    const bool changed = ImGui::InputTextMultiline("##source", doc.text.data(), doc.text.capacity() + 1, ImVec2(-FLT_MIN, height), flags,
        &ScriptEditorPanel::onText, &doc);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
    if (clipboardSwapped) {
        ImGui::SetClipboardText(clipboardBackup.c_str());
    }
    doc.hadFocus = ImGui::GetActiveID() == id;
    if (changed) {
        doc.indexDirty = true;
        doc.validateDue = true;
        doc.editedAt = ImGui::GetTime();
        doc.status.clear();
    }
    if (doc.indexDirty) {
        rebuildIndex(doc);
    }

    // Подсветка и номера строк рисуются поверх прозрачного текста поля, в его же окне и с его же прокруткой.
    // Выделение и курсор поле рисует само: текст идёт сверху выделения, как в обычном редакторе.
    const ImGuiWindow* child = findInputChild(id);
    if (child && !child->Hidden) {
        ImDrawList* drawList = child->DrawList;
        const ImRect inner = child->InnerRect;
        const ImGuiInputTextState* state = ImGui::GetInputTextState(id);
        const float scrollX = state ? state->Scroll.x : 0.0f;
        const ImVec2 origin(inner.Min.x + paddingX - scrollX, inner.Min.y + paddingY - child->Scroll.y);
        const int lineCount = static_cast<int>(doc.lineStarts.size());
        const int first = std::clamp(static_cast<int>(std::floor((inner.Min.y - origin.y) / lineHeight)), 0, lineCount - 1);
        const int last = std::clamp(static_cast<int>(std::floor((inner.Max.y - origin.y) / lineHeight)), 0, lineCount - 1);
        const float textLeft = inner.Min.x + gutterWidth;
        const char* base = doc.text.c_str();
        const char* textEnd = base + doc.text.size();
        const bool hasSelection = doc.selStart != doc.selEnd;

        drawList->PushClipRect(ImVec2(textLeft, inner.Min.y), inner.Max, true);
        for (int line = first; line <= last; ++line) {
            const float y = origin.y + static_cast<float>(line) * lineHeight;
            const char* begin = base + doc.lineStarts[static_cast<std::size_t>(line)];
            const char* end = line + 1 < lineCount ? base + doc.lineStarts[static_cast<std::size_t>(line) + 1] - 1 : textEnd;
            if (doc.hadFocus && !hasSelection && line == doc.line) {
                drawList->AddRectFilled(ImVec2(textLeft, y), ImVec2(inner.Max.x, y + lineHeight), kCurrentLine);
            }
            if (line + 1 == doc.errorLine) {
                drawList->AddRectFilled(ImVec2(textLeft, y), ImVec2(inner.Max.x, y + lineHeight), kErrorLineTint);
                drawList->AddLine(ImVec2(origin.x, y + lineHeight - 1.0f), ImVec2(origin.x + ImGui::CalcTextSize(begin, end).x, y + lineHeight - 1.0f),
                    kError, 1.0f);
            }
            bool inComment = doc.lineInComment[static_cast<std::size_t>(line)] != 0;
            drawHighlightedLine(drawList, ImVec2(origin.x, y), begin, end, SyntaxLanguage::Lua, inComment);
        }
        drawList->PopClipRect();

        // Колонка номеров не уезжает при горизонтальной прокрутке и закрывает текст, ушедший под неё.
        drawList->PushClipRect(inner.Min, inner.Max, true);
        drawList->AddRectFilled(inner.Min, ImVec2(textLeft, inner.Max.y), kGutterBackground);
        drawList->AddLine(ImVec2(textLeft - 0.5f, inner.Min.y), ImVec2(textLeft - 0.5f, inner.Max.y), ImGui::GetColorU32(kBorder));
        char number[16];
        for (int line = first; line <= last; ++line) {
            const float y = origin.y + static_cast<float>(line) * lineHeight;
            const bool isError = line + 1 == doc.errorLine;
            std::snprintf(number, sizeof(number), "%d", line + 1);
            const float numberWidth = ImGui::CalcTextSize(number).x;
            const ImU32 color = isError ? kError : (doc.hadFocus && line == doc.line ? kLineNumberCurrent : kLineNumber);
            drawList->AddText(ImVec2(textLeft - px(10.0f) - numberWidth, y), color, number);
            if (isError) {
                drawList->AddCircleFilled(ImVec2(inner.Min.x + px(8.0f), y + lineHeight * 0.5f), px(2.5f), kError);
            }
        }
        drawList->PopClipRect();
    }
    EditorUI::popFont();
}

void ScriptEditorPanel::drawStatusBar(EditorContext& context, Document& doc) {
    const float height = ImGui::GetFrameHeight();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    EditorUI::pushSmallFont();
    const float textY = position.y + (height - ImGui::GetFontSize()) * 0.5f;

    char info[160];
    // Файл с табами в редакторе показан пробелами и при сохранении получает табы обратно.
    const char* indent = doc.format.tabIndent ? "Tabs" : "Spaces: 4";
    const int lineCount = static_cast<int>(doc.lineStarts.size());
    if (doc.selStart != doc.selEnd) {
        std::snprintf(info, sizeof(info), "Ln %d, Col %d  (%d selected)   %d lines   Lua   %s   UTF-8   %s", doc.line + 1, doc.column + 1,
            std::abs(doc.selEnd - doc.selStart), lineCount, indent, doc.format.crlf ? "CRLF" : "LF");
    } else {
        std::snprintf(info, sizeof(info), "Ln %d, Col %d   %d lines   Lua   %s   UTF-8   %s", doc.line + 1, doc.column + 1, lineCount, indent,
            doc.format.crlf ? "CRLF" : "LF");
    }
    const float infoWidth = ImGui::CalcTextSize(info).x;
    drawList->AddText(ImVec2(position.x + width - infoWidth - px(4.0f), textY), ImGui::GetColorU32(kTextFaint), info);

    // Слева — что произошло с файлом. После сохранения здесь ответ hot reload: «enemy.lua reloaded in Play: ...».
    std::string message;
    ImU32 color = kTextDim;
    const bool reloadAnswer = ImGui::GetTime() - doc.savedAt < kReloadResultSeconds && !context.scriptMessage.empty() &&
        context.scriptMessage != doc.messageAtSave && context.autoReloadScripts;
    if (doc.conflict) {
        message = "Changed on disk: choose which version to keep";
        color = kWarning;
    } else if (doc.missing) {
        message = "File is missing on disk";
        color = kWarning;
    } else if (doc.dirty()) {
        message = "Modified  \xC2\xB7  " + EditorUI::shortcut("Ctrl+S") + " to save";
        color = kWarning;
    } else if (reloadAnswer) {
        message = context.scriptMessage;
        color = context.scriptMessageIsError ? kError : kSuccess;
    } else if (!doc.status.empty()) {
        message = doc.status;
        color = doc.statusIsError ? kError : kSuccess;
    } else {
        message = context.autoReloadScripts ? "Saved changes are applied to the running scripts" : "Reload on save is off";
        color = kTextFaint;
    }
    const std::string fitted = EditorUI::ellipsize(message.c_str(), std::max(40.0f, width - infoWidth - px(24.0f)));
    drawList->AddText(ImVec2(position.x + px(4.0f), textY), ImGui::GetColorU32(color), fitted.c_str());
    EditorUI::popFont();
    ImGui::Dummy(ImVec2(width, height));
}

void ScriptEditorPanel::drawCloseDialog() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kCloseDialog, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }
    Document* doc = find(closeRequest_);
    if (!doc) {
        closeRequest_.clear();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::Text("Save changes to %s before closing?", fileName(doc->path).c_str());
    EditorUI::textFaint("Your changes will be lost if you don't save them.");
    ImGui::Dummy(ImVec2(0.0f, px(4.0f)));
    const bool canSave = !doc->conflict;
    ImGui::BeginDisabled(!canSave);
    if (EditorUI::primaryButton("Save", ImVec2(px(100.0f), 0.0f)) && save(*doc)) {
        closeDocument(closeRequest_);
        closeRequest_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Don't Save", ImVec2(px(100.0f), 0.0f))) {
        closeDocument(closeRequest_);
        closeRequest_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(px(100.0f), 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        closeRequest_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
