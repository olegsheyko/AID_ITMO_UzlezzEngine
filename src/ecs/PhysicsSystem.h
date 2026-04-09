#pragma once

#include "ecs/System.h"

class World;

class PhysicsSystem : public UpdateSystem {
public:
    explicit PhysicsSystem(float gravityStrength = 9.81f);

    void update(World& world, float dt) override;

    float getGravityStrength() const { return gravityStrength_; }
    void setGravityStrength(float gravityStrength) { gravityStrength_ = gravityStrength; }

private:
    float gravityStrength_ = 9.81f;
};
