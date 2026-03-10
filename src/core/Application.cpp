#include "Application.h"
#include "render/OpenGLRenderAdapter.h"
#include "states/LoadingState.h"
#include "states/MenuState.h"
#include "states/GameplayState.h"

bool Application::init(int width, int height, const char* title) {
	LOG_INFO("Application: Initializing application");
	renderer_ = std::make_unique<OpenGLRenderAdapter>();
	
	if (!renderer_->init(width, height, title)) {
		LOG_ERROR("Application: Failed to initialize renderer");
		return false;
	}

	stateManager_.push(std::make_unique<LoadingState>());

	lastFrameTime_ = Clock::now();
	LOG_INFO("Application: Initialization successful");
	return true;
}

void Application::run() {
	LOG_INFO("Application: starting main loop");

	while (renderer_ && renderer_->isRunning()) {
		auto now = Clock::now();
		float dt = std::chrono::duration<float>(now - lastFrameTime_).count();
		lastFrameTime_ = now;
		if (dt > 0.1f) dt = 0.1f;

		renderer_->pollEvents();
		update(dt);
		render();
	}
}

void Application::update(float dt) {
	IGameState* current = stateManager_.current();
	if (!current) return;

	current->update(dt);

	if (auto* loading = dynamic_cast<LoadingState*>(current)) {
		if (loading->isFinished()) {
			stateManager_.change(std::make_unique<MenuState>());
		}
	} else if (auto* menu = dynamic_cast<MenuState*>(current)) {
		if (menu->shouldStartGame()) {
			stateManager_.change(std::make_unique<GameplayState>());
		}
	}
}

void Application::render() {
	auto* current = stateManager_.current();

	renderer_->beginFrame(0.1f, 0.1f, 0.2f);
	if (current) {
		current->render();
	}
	renderer_->endFrame();
}

void Application::shutdown() {
	LOG_INFO("Application: shutting down");

	while (!stateManager_.isEmpty()) {
		stateManager_.pop();
	}

	if (renderer_) {
		renderer_->shutdown();
	}
}
