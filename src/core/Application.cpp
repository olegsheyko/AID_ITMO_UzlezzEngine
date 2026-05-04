#include "Application.h"
#include "input/GlfwInputHandler.h"
#include "input/InputManager.h"
#include "render/OpenGLRenderAdapter.h"
#include "states/EditorState.h"
#include "states/LoadingState.h"
#include "states/MenuState.h"
#include "states/GameplayState.h"
#include "resources/ResourceManager.h"
#include "resources/HotReload.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

bool Application::init(int width, int height, const char* title) {
	LOG_INFO("Application: Initializing application");
	renderer_ = std::make_unique<OpenGLRenderAdapter>();
	
	if (!renderer_->init(width, height, title)) {
		LOG_ERROR("Application: Failed to initialize renderer");
		return false;
	}

    if (auto* openGlRenderer = dynamic_cast<OpenGLRenderAdapter*>(renderer_.get())) {
        InputManager::getInstance().initialize(std::make_unique<GlfwInputHandler>(openGlRenderer->getWindow()));
    }

	if (!initEditorGui()) {
		LOG_ERROR("Application: Failed to initialize editor GUI");
		return false;
	}

	// Инициализируем менеджер ресурсов
	ResourceManager::getInstance().init(renderer_.get());
	LOG_INFO("Application: ResourceManager initialized");

	LOG_INFO("Application: HotReload initialized");

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
        InputManager::getInstance().updateState();
		update(dt);
		render();
	}
}

void Application::update(float dt) {
	// Проверяем изменения файлов для горячей замены
	if (HotReload::getInstance().update()) {
		const auto& changedFiles = HotReload::getInstance().getChangedFiles();
		for (const auto& path : changedFiles) {
			ResourceManager::getInstance().reloadShadersForFile(path);
		}
	}

	IGameState* current = stateManager_.current();
	if (!current) return;

	current->update(dt);

	if (auto* loading = dynamic_cast<LoadingState*>(current)) {
		if (loading->isFinished()) {
			stateManager_.change(std::make_unique<EditorState>(*renderer_));
		}
	} else if (auto* menu = dynamic_cast<MenuState*>(current)) {
		if (menu->shouldStartGame()) {
			stateManager_.change(std::make_unique<GameplayState>(*renderer_));
		}
	}
}

void Application::render() {
	auto* current = stateManager_.current();

	renderer_->beginFrame(0.1f, 0.1f, 0.2f);
	beginEditorGuiFrame();
	if (current) {
		current->render();
	}
	renderEditorGuiFrame();
	renderer_->endFrame();
}

void Application::shutdown() {
	LOG_INFO("Application: shutting down");

	// Очищаем кэш ресурсов
	ResourceManager::getInstance().clearCache();

	while (!stateManager_.isEmpty()) {
		stateManager_.pop();
	}

	shutdownEditorGui();
	if (renderer_) {
		renderer_->shutdown();
	}
    InputManager::getInstance().shutdown();
}

bool Application::initEditorGui() {
	auto* openGlRenderer = dynamic_cast<OpenGLRenderAdapter*>(renderer_.get());
	if (openGlRenderer == nullptr || openGlRenderer->getWindow() == nullptr) {
		return false;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 4.0f;
	style.FrameRounding = 3.0f;
	style.TabRounding = 3.0f;
	style.GrabRounding = 3.0f;
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize = 0.0f;

	if (!ImGui_ImplGlfw_InitForOpenGL(openGlRenderer->getWindow(), true)) {
		return false;
	}
	if (!ImGui_ImplOpenGL3_Init("#version 330")) {
		ImGui_ImplGlfw_Shutdown();
		return false;
	}

	editorGuiInitialized_ = true;
	return true;
}

void Application::beginEditorGuiFrame() {
	if (!editorGuiInitialized_) {
		return;
	}

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	ImGuizmo::BeginFrame();
}

void Application::renderEditorGuiFrame() {
	if (!editorGuiInitialized_) {
		return;
	}

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Application::shutdownEditorGui() {
	if (!editorGuiInitialized_) {
		return;
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	editorGuiInitialized_ = false;
}
