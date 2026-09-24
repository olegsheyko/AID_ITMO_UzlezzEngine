#pragma once
#include "states/IGameState.h"
#include "core/Logger.h"
#include "resources/Resource.h"
#include "resources/SceneManifest.h"

class LoadingState : public IGameState {
public:
    void onEnter() override;
    void onExit() override;
    void update(float dt) override;
    void render() override;

    bool isFinished() const { return finished_; }

private:
    std::shared_ptr<Resource<SceneManifest>> scene_;
    bool finished_ = false;
};