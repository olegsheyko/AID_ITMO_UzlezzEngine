#pragma once
#include "states/IGameState.h"
#include "core/Logger.h"

// Main menu state. Press Enter to start the game.
class MenuState : public IGameState {
public:
    void onEnter() override;
    void onExit() override;
    void update(float dt) override;
    void render() override;

    bool shouldStartGame() const { return startGame_; }

private:
    bool startGame_ = false;
};
