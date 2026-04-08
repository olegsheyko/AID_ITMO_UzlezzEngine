#pragma once

#include "ecs/System.h"

class IRenderAdapter;
class World;

class DebugRenderSystem : public RenderSystemBase {
public:
    explicit DebugRenderSystem(IRenderAdapter& renderer);

    void render(World& world) override;
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

private:
    IRenderAdapter& renderer_;
    bool enabled_ = false;
};
