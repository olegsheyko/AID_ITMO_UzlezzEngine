#pragma once

#include "ecs/System.h"

class IRenderAdapter;

class CameraSystem : public UpdateSystem {
public:
    explicit CameraSystem(IRenderAdapter& renderer);

    void update(World& world, float dt) override;

private:
    IRenderAdapter& renderer_;
};
