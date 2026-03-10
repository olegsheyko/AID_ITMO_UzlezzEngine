#include "states/LoadingState.h"

void LoadingState::onEnter() {
    LOG_INFO("LoadingState: entered");
    timer_ = 0.0f;
    finished_ = false;
}

void LoadingState::onExit() {
    LOG_INFO("LoadingState: exited");
}

void LoadingState::update(float dt) {
    timer_ += dt; // Accumulate elapsed loading time.

    if (timer_ >= duration_) {
        finished_ = true; // The loading timeout has elapsed.
    }
}

void LoadingState::render() {
    // Rendering is intentionally empty for now.
}
