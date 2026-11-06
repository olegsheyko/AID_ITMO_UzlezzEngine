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
// Панель живёт в EditorState (поле scriptEditor_). Другие панели сами файл не открывают: кладут путь в
// EditorContext::openScriptRequest, а draw() забирает запрос в начале кадра.
// Текст с диска и текст в поле ввода приводятся к одному виду функциями из ScriptText (там же правила отступов).
class ScriptEditorPanel {
public:
    // Один кадр окна: разбирает запрос на открытие, раз в полсекунды сверяет файлы с диском, рисует вкладки.
    void draw(EditorContext& context);

    // Открывает скрипт во вкладке (или переключается на уже открытую). line с 1 — перейти к строке.
    bool openFile(const std::string& path, int line = 0);
    // Редактор открывает только файлы с расширением .lua (регистр не важен).
    static bool isScript(const std::string& path);

    // Для вопроса при выходе из редактора и для пункта меню Save Scripts. Файлы, которые не удалось
    // открыть для правки (loadError), не считаются: сохранять в них нечего.
    bool hasUnsavedChanges() const;
    std::vector<std::string> unsavedFiles() const;
    // Сохраняет все изменённые скрипты. false — хотя бы один не записался (причина в логе).
    bool saveAll();
    // Закрыть без сохранения: при выходе, когда пользователь выбрал «Quit Without Saving».
    void discardAll();

    bool open = false;

private:
    // Один открытый скрипт (одна вкладка): текст в буфере, его копия «как на диске» и всё состояние окна
    // для этого файла: курсор, ошибка синтаксиса, баннеры, строка статуса. Документы живут в documents_.
    struct Document {
        std::string path;
        std::string text;  // буфер InputText: '\n', без табов и BOM
        std::string saved; // что сейчас на диске (в том же виде); text != saved — есть несохранённые правки
        // Как файл лежал на диске (CRLF, табы, BOM): при сохранении возвращаем как было.
        ScriptText::DiskFormat format;
        // Метка времени и размер файла при последнем чтении или записи: по ним checkDisk() замечает правку снаружи.
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
        // При переходе к строке прокрутить так, чтобы курсор оказался посередине окна.
        bool centerCursor = false;
        // Перевести клавиатурный фокус в поле ввода в ближайшем кадре.
        bool focusRequest = false;
        bool resetInput = false;  // текст заменён снаружи: поле должно перечитать буфер
        // Поле ввода было активно в последнем нарисованном кадре (от этого зависит подсветка текущей строки).
        bool hadFocus = false;

        // Индекс строк для рисования; пересобирается после правок.
        // Смещение начала каждой строки в text.
        std::vector<int> lineStarts;
        // 1, если строка начинается внутри блочного комментария --[[ ]].
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

        // Строка статуса слева внизу и «ответ hot reload» после сохранения:
        // savedAt это время сохранения, messageAtSave это EditorContext::scriptMessage в тот момент.
        // Если позже сообщение сменилось, значит вотчер успел применить файл, и его ответ показываем вместо «Saved».
        std::string status;
        bool statusIsError = false;
        double savedAt = -1000.0;
        std::string messageAtSave;

        bool dirty() const { return text != saved; }
    };

    // Колбэк поля ввода: ресайз буфера, фильтр Tab, автоотступ после Enter, Tab и Shift+Tab по блоку строк,
    // а также запись позиции курсора обратно в Document. Document приходит через UserData.
    static int onText(ImGuiInputTextCallbackData* data);

    Document* find(const std::string& path) const;
    // Документ вкладки, открытой в этом кадре, или nullptr.
    Document* active() const;
    // Чтение с диска, проверка размера и «не бинарный ли файл»; при отказе пишет причину в loadError.
    void load(Document& doc);
    // Запись в прежнем формате (CRLF, табы, BOM). false, если файл не записался: причина в doc.status и в логе.
    bool save(Document& doc);
    void revert(Document& doc);
    // Подмена всего текста не из поля ввода (Revert, перечитывание с диска): сбрасывает индекс, проверку и состояние поля.
    void replaceText(Document& doc, std::string text);
    void rebuildIndex(Document& doc);
    // Проверка синтаксиса без запуска скрипта (ScriptWatcher::validate); результат в doc.error и doc.errorLine.
    void validate(Document& doc);
    // Опрос диска: исчез ли файл, изменили ли его снаружи, и что делать с версией в буфере.
    void checkDisk(Document& doc);
    // Ставит курсор в начало строки line (с 1), прокручивает к ней и возвращает фокус полю.
    void gotoLine(Document& doc, int line);
    void closeDocument(const std::string& path);

    void drawEmpty(EditorContext& context);
    void drawDocument(EditorContext& context, Document& doc);
    void drawToolbar(EditorContext& context, Document& doc);
    // Плашки над текстом: конфликт с диском, файл удалён, ошибка синтаксиса (клик по ней ведёт к строке).
    void drawBanners(Document& doc);
    // Само поле ввода, подсветка Lua и колонка номеров строк. Самая сложная часть окна.
    void drawEditor(Document& doc, float height);
    void drawStatusBar(EditorContext& context, Document& doc);
    // Модальное окно «Save changes to ... before closing?» при закрытии вкладки с несохранёнными правками.
    void drawCloseDialog();

    // unique_ptr, чтобы указатели на Document не менялись, когда вектор растёт или вкладку закрывают.
    std::vector<std::unique_ptr<Document>> documents_;
    std::string selectRequest_;  // вкладка, которую нужно вывести на передний план
    std::string activePath_;     // вкладка, открытая в этом кадре
    std::string closeRequest_;   // закрытие вкладки с несохранёнными правками ждёт ответа
    // Когда в последний раз опрашивали диск (раз в kDiskPollSeconds).
    double lastDiskCheck_ = 0.0;
    std::string lastScriptMessage_; // EditorContext::scriptMessage на начало кадра
};
