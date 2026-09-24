#pragma once
#include "states/IGameState.h"
#include "ecs/World.h"
#include "ecs/AnimationSystem.h"
#include "ecs/RenderSystem.h"
#include "bench/LoadScenario.h"
#include <chrono>

// Fixed, actually rendered scene; raw full-frame timings arrive from Application.
class AnimationBenchmark : public IGameState {
public:
    struct Config {
        bool parallel = true;
        unsigned int characters = 512;
        unsigned int frames = 360;
        unsigned int warmup = 120;
        bool waitTracy = false;
        bool exitLoading = false;
        std::string output = "bench/animation.csv";
    };
    AnimationBenchmark(IRenderAdapter& renderer, Config config);
    void onEnter() override;
    void update(float dt) override;
    void render() override;
    void beginFrame(double previousFrameMs);
    bool finished() const { return finished_; }
    bool failed() const { return failed_; }
private:
    void finish(bool complete);
    void createScene();
    std::shared_ptr<Resource<MeshData>> mesh_;
    struct Sample { double frame, animation, render; };
    IRenderAdapter& renderer_;
    Config config_;
    World world_;
    AnimationSystem animation_;
    RenderSystem render_;
    LoadScenario loads_;
    std::vector<Entity> characters_;
    std::vector<Sample> samples_;
    std::chrono::steady_clock::time_point started_;
    unsigned int frame_ = 0;
    bool ready_ = false, previousMeasured_ = false, finished_ = false, failed_ = false, exiting_ = false;
    double renderMs_ = 0;
};
