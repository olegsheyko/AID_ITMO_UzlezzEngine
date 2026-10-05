#pragma once

class EditorContext;

class GameViewPanel {
public:
    void draw(EditorContext& context);

    bool open = true;
    // Индекс пресета разрешения из списка в GameViewPanel.cpp.
    int resolution = 0;
    bool showStats = false;

private:
    void drawToolbar(EditorContext& context);
};
