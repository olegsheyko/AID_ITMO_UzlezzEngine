#pragma once

class EditorContext;

// Статистика кадра и ресурсов плюс инструменты лабораторных (загрузка, стресс, толпа с анимацией).
class RendererInfoPanel {
public:
    void draw(EditorContext& context);

    bool open = true;

private:
    void drawFrame(EditorContext& context);
    void drawScene(EditorContext& context);
    void drawResources(EditorContext& context);
    void drawAnimation(EditorContext& context);
    void drawHeavyLoad(EditorContext& context);
    void drawStress(EditorContext& context);
};
