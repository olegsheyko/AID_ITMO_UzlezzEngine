#pragma once
#include "ecs/Components.h"
#include "jobs/JobSystem.h"
#include <vector>
class World;
class AnimationSystem {
public:
    bool parallel = true;
    bool paused = false;
    float speed = 1;
    unsigned int batchSize = 32;
    double lastUpdateMs = 0;
    size_t characterCount = 0;
    void update(World& world, float dt);
private:
    struct Work { const MeshData* mesh; Animator* animator; };
    std::vector<Work> work_;
    std::vector<JobHandle> jobs_;
    void evaluateRange(size_t begin, size_t end);
};
