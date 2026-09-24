#include "states/LoadingState.h"
#include "core/Logger.h"
#include "resources/ResourceManager.h"

void LoadingState::onEnter() {
    LOG_INFO("LoadingState: entered");
    scene_ = ResourceManager::getInstance().loadSceneAsync("assets/scenes/demo_scene.json");
    finished_ = false;
}

void LoadingState::onExit() {
    LOG_INFO("LoadingState: exited");
}

void LoadingState::update(float) {
    finished_ = !scene_ || !scene_->isPending();
}

void LoadingState::render() {
    // Rendering is intentionally empty for now.
}
