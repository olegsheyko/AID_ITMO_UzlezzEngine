#pragma once

#include "editor/ScriptText.h"

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class EditorContext;

// Редактор Lua-скриптов прямо в окне редактора: вкладки, подсветка, номера строк, отступы по Tab и Enter,
// проверка синтаксиса на лету, Ctrl+S. Сохранённый файл подхватывает тот же вотчер hot reload, что и при правке
// во внешнем редакторе, поэтому в Play изменения видны в игре сразу. Внешний редактор остаётся: «Open Externally»,
// а правки, сделанные снаружи, подтягиваются в буфер (или предлагают выбрать версию, если в буфере есть несохранённое).
class ScriptEditorPanel {
public:
    void draw(EditorContext& context);

    // Открывает скрипт во вкладке (или переключается на уже открытую). line с 1 — перейти к строке.
    bool openFile(const std::string& path, int line = 0);
    static bool isScript(const std::string& path);

    bool hasUnsavedChanges() const;
    std::vector<std::string> unsavedFiles() const;
    // Сохраняет все изменённые скрипты. false — хотя бы один не записался (причина в логе).
    bool saveAll();
    // Закрыть без сохранения: при выходе, когда пользователь выбрал «Quit Without Saving».
    void discardAll();

    bool open = false;

private:
    struct Document {
        std::string path;
        std::string text;  // буфер InputText: '\n', без табов и BOM
        std::string saved; // что сейчас на диске (в том же виде); text != saved — есть несохранённые правки
        ScriptText::DiskFormat format;
        std::filesystem::file_time_type diskTime{};
        std::uintmax_t diskSize = 0;
        bool missing = false;  // файл исчез с диска
        bool conflict = false; // на диске другая версия, а в буфере несохранённые правки
        std::string diskCopy;  // версия с диска при конфликте
        std::string loadError; // файл нельзя открыть для правки: слишком большой, бинарный, нечитаемый

        // Курсор: смещения в байтах и строка/колонка с 0. Обновляет колбэк InputText, пока поле активно.
        int cursor = 0;
        int selStart = 0;
        int selEnd = 0;
        int line = 0;
        int column = 0;
        int pendingCursor = -1;   // куда поставить курсор в ближайшем кадре
        bool centerCursor = false;
        bool focusRequest = false;
        bool resetInput = false;  // текст заменён снаружи: поле должно перечитать буфер
        bool hadFocus = false;

        // Индекс строк для рисования; пересобирается после правок.
        std::vector<int> lineStarts;
        std::vector<char> lineInComment;
        bool indexDirty = true;

        // Проверка синтаксиса: отложена на паузу в наборе.
        bool validateDue = true;
        double editedAt = 0.0;
        std::string error;
        int errorLine = 0; // с 1, 0 — ошибок нет или у ошибки нет строки

        // Нажатия этого кадра, которые разбирает колбэк.
        bool tabKey = false;
        bool enterKey = false;

        std::string status;
        bool statusIsError = false;
        double savedAt = -1000.0;
        std::string messageAtSave;

        bool dirty() const { return text != saved; }
    };

    static int onText(ImGuiInputTextCallbackData* data);

    Document* find(const std::string& path) const;
    Document* active() const;
    void load(Document& doc);
    bool save(Document& doc);
    void revert(Document& doc);
    void replaceText(Document& doc, std::string text);
    void rebuildIndex(Document& doc);
    void validate(Document& doc);
    void checkDisk(Document& doc);
    void gotoLine(Document& doc, int line);
    void closeDocument(const std::string& path);

    void drawEmpty(EditorContext& context);
    void drawDocument(EditorContext& context, Document& doc);
    void drawToolbar(EditorContext& context, Document& doc);
    void drawBanners(Document& doc);
    void drawEditor(Document& doc, float height);
    void drawStatusBar(EditorContext& context, Document& doc);
    void drawCloseDialog();

    std::vector<std::unique_ptr<Document>> documents_;
    std::string selectRequest_;  // вкладка, которую нужно вывести на передний план
    std::string activePath_;     // вкладка, открытая в этом кадре
    std::string closeRequest_;   // закрытие вкладки с несохранёнными правками ждёт ответа
    double lastDiskCheck_ = 0.0;
    std::string lastScriptMessage_; // EditorContext::scriptMessage на начало кадра
};
