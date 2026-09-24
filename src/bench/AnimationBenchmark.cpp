#include "bench/AnimationBenchmark.h"
#include "resources/ResourceManager.h"
#include "render/IRenderAdapter.h"
#include "math/CameraMath.h"
#include "core/Logger.h"
#include <tracy/Tracy.hpp>
#include <filesystem>
#include <fstream>
#include <cmath>

AnimationBenchmark::AnimationBenchmark(IRenderAdapter& renderer, Config config)
    : renderer_(renderer), config_(std::move(config)), render_(renderer) {}

void AnimationBenchmark::onEnter() {
    started_=std::chrono::steady_clock::now();
    auto& resources=ResourceManager::getInstance();
    mesh_=resources.loadMeshAsync("assets/models/animation/Walking.fbx");
}
void AnimationBenchmark::createScene() {
    auto& resources=ResourceManager::getInstance();
    auto mesh=mesh_;
    auto shader=resources.loadShader("assets/shaders/mesh_vertex.glsl","assets/shaders/mesh_fragment_simple_texture.glsl");
    if (!mesh || !mesh->isLoaded() || !shader || mesh->getData()->skeleton.clips.empty()) { failed_=true; finish(false); return; }
    const int columns=static_cast<int>(std::ceil(std::sqrt(float(config_.characters))));
    for (unsigned int i=0;i<config_.characters;++i) {
        Entity e=world_.createEntity(); characters_.push_back(e);
        auto& transform=world_.addComponent<Transform>(e);
        transform.position={3.0f*(int(i)%columns-(columns-1)*.5f),3.0f*(int(i)/columns-(columns-1)*.5f),0};
        transform.scale={2.0f/181,2.0f/181,2.0f/181}; transform.rotation.x=1.57079632679f;
        auto& mr=world_.addComponent<MeshRenderer>(e); mr.cachedMesh=mesh; mr.cachedShader=shader;
        world_.addComponent<Animator>(e);
    }
    Entity camera=world_.createEntity(); world_.addComponent<Transform>(camera);
    auto& c=world_.addComponent<Camera>(camera);
    const float distance=columns*5.5f;
    c.viewMatrix=CameraMath::view({0,-distance*.8660254f,distance*.5f+1},-.5235988f,0);
    c.projectionMatrix=Math::perspective(.785398163f,800.0f/600,.1f,1000);
    animation_.parallel=config_.parallel;
    samples_.reserve(config_.frames);
    LOG_INFO("Animation benchmark: "+std::to_string(config_.characters)+" characters, "+(config_.parallel ? "parallel" : "sequential"));
}
void AnimationBenchmark::beginFrame(double previousFrameMs) {
    if (finished_) return;
    if (previousMeasured_) samples_.push_back({previousFrameMs,animation_.lastUpdateMs,renderMs_});
    previousMeasured_=false;
    if (exiting_) { finish(false); return; }
    if (samples_.size()>=config_.frames) finish(true);
}
void AnimationBenchmark::update(float) {
    if (finished_) return;
    if (characters_.empty()) {
        if (mesh_ && mesh_->isPending()) return;
        createScene();
        if (finished_) return;
    }
    if (config_.waitTracy && !ready_) {
#ifdef TRACY_ENABLE
        if (!TracyIsConnected) {
            if (std::chrono::duration<double>(std::chrono::steady_clock::now()-started_).count()>30) { failed_=true; finish(false); }
            return;
        }
#else
        failed_=true; finish(false); return;
#endif
    }
    ready_=true;
    const double duration=world_.getComponent<MeshRenderer>(characters_[0]).cachedMesh->getData()->skeleton.clips[0].duration;
    // Deterministic playback: same phases for the same frame in both modes.
    for (size_t i=0;i<characters_.size();++i)
        world_.getComponent<Animator>(characters_[i]).time=Animation::wrapTime(frame_/60.0+duration*i/characters_.size(),duration);
    animation_.update(world_,0);
    previousMeasured_=frame_>=config_.warmup;
    if (frame_==config_.warmup) TracyMessageL("Animation measured window begins");
    if (config_.exitLoading && frame_==config_.warmup) {
        loads_.start(LoadScenario::Mode::Burst,true);
        loads_.update();
        ResourceManager::getInstance().loadMeshAsync("assets/models/animation/Sneak_Walk.fbx");
        ResourceManager::getInstance().loadSceneAsync("assets/scenes/demo_scene.json");
        LOG_INFO("Animation exit during load: pending="+std::to_string(ResourceManager::getInstance().pendingLoadCount()));
        exiting_=true;
    }
    ++frame_;
}
void AnimationBenchmark::render() {
    auto start=std::chrono::steady_clock::now();
    render_.render(world_);
    renderMs_=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
void AnimationBenchmark::finish(bool complete) {
    finished_=true;
    const std::filesystem::path path(config_.output);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    if (!file) { failed_=true; LOG_ERROR("Cannot write animation benchmark CSV"); return; }
    file << "# mode=" << (config_.parallel ? "parallel" : "sequential") << "\n# characters=" << config_.characters
        << "\n# workers=" << JobSystem::getInstance().workerCount() << "\n# batch=" << animation_.batchSize
        << "\n# warmup=" << config_.warmup << "\n# vsync=0\n# resolution=800x600\n# complete=" << complete
        << "\n# exit_loading=" << config_.exitLoading << "\n# pending_at_exit=" << ResourceManager::getInstance().pendingLoadCount()
        << "\nframe,frame_ms,animation_ms,render_submit_ms\n";
    for (size_t i=0;i<samples_.size();++i) file << i << ',' << samples_[i].frame << ',' << samples_[i].animation << ',' << samples_[i].render << '\n';
    LOG_INFO("Animation benchmark CSV: "+config_.output);
}
