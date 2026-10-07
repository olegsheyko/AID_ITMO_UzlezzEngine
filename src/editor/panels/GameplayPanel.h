#pragma once

class EditorContext;

// Lua-геймплей (ЛР 2): сцена Arena, перезагрузка скриптов, управление, статус игры и ошибки Lua.
class GameplayPanel {
public:
    void draw(EditorContext& context);

    bool open = true;

private:
    void drawRuntime(EditorContext& context);
    void drawControls();
    void drawStatus(EditorContext& context);
    void drawScriptedEntities(EditorContext& context);
};
