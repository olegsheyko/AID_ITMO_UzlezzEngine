#pragma once

#include "ecs/System.h"

#include <cstddef>

class World;

class PhysicsSystem : public UpdateSystem {
public:
    explicit PhysicsSystem(float gravityStrength = 9.81f);

    void update(World& world, float dt) override;

    float getGravityStrength() const { return gravityStrength_; }
    void setGravityStrength(float gravityStrength) { gravityStrength_ = gravityStrength; }
    std::size_t getLastCollisionCount() const { return lastCollisionCount_; }

private:
    float gravityStrength_ = 9.81f;
    std::size_t lastCollisionCount_ = 0;
};
