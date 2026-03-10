#pragma once
#include "states/IGameState.h"
#include "core/Logger.h"

class LoadingState : public IGameState {
public:
    void onEnter() override;
    void onExit() override;
    void update(float dt) override;
    void render() override;

    bool isFinished() const { return finished_; }

private:
    float timer_ = 0.0f;         
    const float duration_ = 2.0f; 
    bool finished_ = false;
};