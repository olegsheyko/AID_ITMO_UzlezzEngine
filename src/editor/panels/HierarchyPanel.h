#pragma once

#include "ecs/Entity.h"

#include <array>

class EditorContext;

class HierarchyPanel {
public:
    void draw(EditorContext& context);

    bool open = true;

private:
    void drawEntity(EditorContext& context, Entity entity, int depth);
    void drawSearchResults(EditorContext& context);
    void drawRowContents(EditorContext& context, Entity entity, float labelX, bool showParent);
    void entityContextMenu(EditorContext& context, Entity entity);
    void createMenuItems(EditorContext& context, Entity parent);
    void acceptDrop(EditorContext& context, Entity target);

    std::array<char, 128> search_{};
    Entity scrollTo_ = 0;
    Entity lastSelected_ = 0;
    bool revealSelection_ = false;
};
