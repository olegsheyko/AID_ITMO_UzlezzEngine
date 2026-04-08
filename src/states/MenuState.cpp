#include "states/MenuState.h"
#include "core/Logger.h"
#include "input/InputManager.h"
#include "input/KeyCode.h"

void MenuState::onEnter() {
    LOG_INFO("MenuState: entered");
    startGame_ = false;
}

void MenuState::onExit() {
    LOG_INFO("MenuState: exited");
}

void MenuState::update(float dt) {
    (void)dt;
    // Start the game once Enter is pressed.
    if (InputManager::getInstance().isKeyPressed(KeyCode::Enter)) {
        startGame_ = true;
    }
}

void MenuState::render() {
    // Rendering is intentionally empty for now.
}
