#pragma once
#include "states/IGameState.h"
#include "ecs/World.h"
#include "ecs/Components.h"
#include "ecs/RenderSystem.h"
#include "ecs/AnimationSystem.h"
#include <string>

// Acceptance harness. Scene generation only creates/edits components.
class CullingBenchmark : public IGameState {
public:
    struct Config {
        bool culling=true, sorting=true, depthSort=true, live=false, verify=false, animated=false;
        unsigned int frames=360, warmup=120;
        std::string output="bench/culling.csv";
    };
    CullingBenchmark(IRenderAdapter& renderer,Config config);
    void onEnter() override;
    void onExit() override;
    void update(float dt) override;
    void render() override;
    void beginFrame(double frameMs);
    bool finished() const { return finished_; }
    bool failed() const { return failed_; }
private:
    Entity addObject(unsigned int index);
    void setCamera(unsigned int frame);
    void finish();
    void verifyImages();
    IRenderAdapter& renderer_;
    Config config_;
    World world_;
    RenderSystem render_;
    AnimationSystem animation_;
    Entity camera_=kInvalidEntity;
    std::vector<Entity> objects_;
    std::shared_ptr<Resource<MeshData>> mesh_;
    std::shared_ptr<Resource<ShaderData>> shader_;
    std::vector<std::shared_ptr<Resource<TextureData>>> textures_;
    struct Sample { double frame; RenderSystem::Stats stats; };
    std::vector<Sample> samples_;
    unsigned int frame_=0, serial_=10000;
    bool previousMeasured_=false, finished_=false, failed_=false, fly_=true, move_=false;
    double lastFrameMs_=0;
    Vec3 eye_{0,-35,14};
    float yaw_=0,pitch_=-.12f;
    RenderSystem::Stats lastStats_;
    double modeTotal_[4]{};
    unsigned int modeSamples_[4]{};
};
