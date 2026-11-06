#pragma once

class EditorContext;

// Lua-геймплей (ЛР 2): сцена Arena, перезагрузка скриптов, управление, статус игры и ошибки Lua.
// Панель ничего не хранит о скриптах сама: всё берёт из EditorContext (scripts, scriptMessage, autoReloadScripts, playReloadMode)
// и рисует каждый кадр заново. Создаётся в EditorState (поле gameplay_).
class GameplayPanel {
public:
    // Окно Gameplay: четыре секции подряд (Lua, Status, Controls, Scripted Entities).
    void draw(EditorContext& context);

    bool open = true;

private:
    // Секция Lua: версия, состояние, память и время скриптов; кнопки Open Arena и Reload Scripts; настройки hot reload.
    void drawRuntime(EditorContext& context);
    // Подсказка по клавишам игры Arena.
    void drawControls();
    // Состояние игры: плитки метрик из статуса Lua, поражение, последнее сообщение скриптинга и ошибки Lua.
    void drawStatus(EditorContext& context);
    // Список сущностей сцены, у которых есть скрипт; клик выделяет сущность.
    void drawScriptedEntities(EditorContext& context);
};
