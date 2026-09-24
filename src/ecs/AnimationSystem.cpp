#include "ecs/AnimationSystem.h"
#include "ecs/World.h"
#include <tracy/Tracy.hpp>
#include <algorithm>
#include <chrono>

void AnimationSystem::evaluateRange(size_t begin, size_t end) {
    ZoneScopedNC("Animation poses", 0x65B891);
    for (size_t i=begin; i<end; ++i) {
        auto& item = work_[i];
        Animation::evaluate(*item.mesh, item.animator->clip, item.animator->time, item.animator->pose);
        item.animator->evaluatedMesh = item.mesh;
    }
}
void AnimationSystem::update(World& world, float dt) {
    ZoneScopedNC("Animation update", 0x438F78);
    const auto start = std::chrono::steady_clock::now();
    {
        ZoneScopedNC("Animation prepare", 0x72A9DA);
        work_.clear(); jobs_.clear();
        world.forEach<MeshRenderer>([&](Entity entity, MeshRenderer& renderer) {
            if (!renderer.cachedMesh || !renderer.cachedMesh->isLoaded()) return;
            const auto* mesh = renderer.cachedMesh->getData();
            if (mesh->skeleton.nodes.empty()) return;
            if (!world.hasComponent<Animator>(entity)) world.addComponent<Animator>(entity);
            auto& animator = world.getComponent<Animator>(entity);
            if (animator.clip >= mesh->skeleton.clips.size()) animator.clip = 0;
            if (!paused && !animator.paused && !mesh->skeleton.clips.empty())
                animator.time = Animation::wrapTime(animator.time + dt*speed*animator.speed,
                    mesh->skeleton.clips[animator.clip].duration);
            Animation::preparePose(*mesh, animator.pose);
            work_.push_back({mesh, &animator});
        });
    }
    characterCount = work_.size();
    if (parallel && !work_.empty()) {
        auto& scheduler = JobSystem::getInstance();
        {
            ZoneScopedNC("Animation dispatch", 0xD4B169);
            // Batches, not a separate job/zone for each bone or character.
            const size_t batch = std::max(1u, batchSize);
            for (size_t begin=0; begin<work_.size(); begin+=batch) {
                const size_t end = std::min(begin+batch, work_.size());
                jobs_.push_back(scheduler.submit([this,begin,end] { evaluateRange(begin,end); }, JobPriority::High));
            }
        }
        {
            ZoneScopedNC("Animation wait", 0xDB956A);
            for (const auto& job : jobs_) scheduler.wait(job);
        }
    } else evaluateRange(0, work_.size());
    lastUpdateMs = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    TracyPlot("Animated characters", static_cast<int64_t>(characterCount));
}
