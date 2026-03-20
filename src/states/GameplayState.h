#pragma once
#include "states/IGameState.h"
#include "core/Logger.h"
#include "ecs/Entity.h"
#include "ecs/RenderSystem.h"
#include "ecs/SpinSystem.h"
#include "ecs/World.h"

class IRenderAdapter;

// Main gameplay state.
class GameplayState : public IGameState {
public:
	explicit GameplayState(IRenderAdapter& renderer);

	void onEnter() override;
	void onExit() override;
	void update(float dt) override;
	void render() override;

private:
	void createScene();
	bool createSceneFromManifest();
	void handleInput(float dt);
	void attachChild(Entity parent, Entity child);

	World world_;
	SpinSystem spinSystem_;
	RenderSystem renderSystem_;
	Entity controllableEntity_ = kInvalidEntity;
	bool rmbWasPressed_ = false; // Edge detection for the right mouse button.
	bool lmbWasPressed_ = false; // Edge detection for the left mouse button.
	bool mmbWasPressed_ = false; // Edge detection for the middle mouse button.
};
