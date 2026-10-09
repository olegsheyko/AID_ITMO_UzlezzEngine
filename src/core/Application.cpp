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
#include "jobs/JobSystem.h"
#include "editor/EditorTheme.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <stb_image_write.h>
#include <tracy/Tracy.hpp>

#include <filesystem>
#include <vector>

bool Application::init(int width, int height, const char* title, const LaunchOptions& options) {
    ZoneScoped;
	LOG_INFO("Application: Initializing application");
	options_ = options;
	auto openGl = std::make_unique<OpenGLRenderAdapter>();
	const bool screenshotMode = !options.editor.screenshotPath.empty();
	if (screenshotMode) {
		// Скриншот снимается со скрытого окна фиксированного размера: кадр рисуется во внеэкранный буфер.
		openGl->setHiddenWindow(true);
		openGl->setOffscreenCapture(true);
		width = options.editor.windowWidth;
		height = options.editor.windowHeight;
	}
	renderer_ = std::move(openGl);

	if (!renderer_->init(width, height, title)) {
		LOG_ERROR("Application: Failed to initialize renderer");
		return false;
	}

	renderer_->setVSync(options.vsync);
	LOG_INFO(std::string("Application: vsync ") + (options.vsync ? "on" : "off"));
	if (options.stress) {
		stress_ = std::make_unique<StressRun>(*renderer_, *options.stress);
		LOG_INFO("Application: stress mode for " + std::to_string(options.stress->durationSeconds) + " s");
	}
	if (options.bench) {
		benchmark_ = std::make_unique<Benchmark>(*options.bench);
		LOG_INFO(std::string("Application: benchmark mode, scenario ") + LoadScenario::modeName(options.bench->mode));
	}

    if (auto* openGlRenderer = dynamic_cast<OpenGLRenderAdapter*>(renderer_.get())) {
        InputManager::getInstance().initialize(std::make_unique<GlfwInputHandler>(openGlRenderer->getWindow()));
    }

	if (!initEditorGui()) {
		LOG_ERROR("Application: Failed to initialize editor GUI");
		return false;
	}

	// Job system — раньше ресурсов: загрузка ставит в неё задачи.
	if (!JobSystem::getInstance().init()) {
		LOG_ERROR("Application: Failed to start job system");
		return false;
	}

	// Инициализируем менеджер ресурсов
	ResourceManager::getInstance().init(renderer_.get());
	if (options.uploadBudgetMs >= 0.0) {
		ResourceManager::getInstance().setUploadBudget(options.uploadBudgetMs);
	}
	LOG_INFO("Application: upload budget " + std::to_string(ResourceManager::getInstance().uploadBudgetMs()) + " ms per frame");
	LOG_INFO("Application: ResourceManager initialized");

	LOG_INFO("Application: HotReload initialized");

	if (options.animationBench) {
        stateManager_.push(std::make_unique<AnimationBenchmark>(*renderer_,*options.animationBench));
        if (stress_) stress_->start();
    } else stateManager_.push(std::make_unique<LoadingState>());

	lastFrameTime_ = Clock::now();
	LOG_INFO("Application: Initialization successful");
	return true;
}

void Application::run() {
	LOG_INFO("Application: starting main loop");
#ifdef TRACY_ENABLE
    tracy::SetThreadName("Main");
#endif

	while (renderer_ && renderer_->isRunning()) {
        ZoneScopedN("Frame");
		auto now = Clock::now();
		const double frameMs = std::chrono::duration<double, std::milli>(now - lastFrameTime_).count();
		lastFrameTime_ = now;
		// Ограничение нужно симуляции; бенчмарк получает честное время кадра до него.
		float dt = static_cast<float>(frameMs / 1000.0);
		if (dt > 0.1f) dt = 0.1f;

		if (benchmark_) {
			benchmark_->beginFrame(frameMs);
			if (benchmark_->isFinished()) {
				break;
			}
		}
        if (auto* animation=dynamic_cast<AnimationBenchmark*>(stateManager_.current())) {
            animation->beginFrame(frameMs);
            if (animation->finished()) { exitCode_=animation->failed() ? 1 : 0; break; }
        }
		if (stress_) {
			stress_->onFrame(frameMs);
			if (stress_->isFinished()) {
				break;
			}
		}

        {
            ZoneScopedN("Input");
            renderer_->pollEvents();
            InputManager::getInstance().updateState();
        }
		update(dt);
		render();
        FrameMark;
	}
}

void Application::update(float dt) {
    ZoneScoped;
	JobSystem::getInstance().collectCompleted();
	// До логики состояния: текстуры, залитые в этом кадре, уже видны его рендеру.
	ResourceManager::getInstance().pumpUploads();

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

	// Закрыли окно, а в редакторе скриптов есть несохранённое: оставляем окно и спрашиваем. Режимы со скриншотами
	// и сценариями закрывают окно сами и спрашивать не должны.
	if (auto* editor = dynamic_cast<EditorState*>(current); editor && options_.editor.screenshotPath.empty()) {
		if (auto* openGlRenderer = dynamic_cast<OpenGLRenderAdapter*>(renderer_.get()); openGlRenderer && openGlRenderer->getWindow()) {
			GLFWwindow* window = openGlRenderer->getWindow();
			if (editor->quitConfirmed()) {
				glfwSetWindowShouldClose(window, GLFW_TRUE);
			} else if (glfwWindowShouldClose(window) && editor->hasUnsavedScripts()) {
				glfwSetWindowShouldClose(window, GLFW_FALSE);
				editor->askToQuit();
			}
		}
	}

	if (auto* loading = dynamic_cast<LoadingState*>(current)) {
		if (loading->isFinished()) {
			stateManager_.change(std::make_unique<EditorState>(*renderer_, options_.editor));
			if (benchmark_) {
				benchmark_->onSceneReady();
			}
			if (stress_) {
				stress_->start();
			}
		}
	} else if (auto* menu = dynamic_cast<MenuState*>(current)) {
		if (menu->shouldStartGame()) {
			stateManager_.change(std::make_unique<GameplayState>(*renderer_));
		}
	}
}

void Application::render() {
    ZoneScoped;
	auto* current = stateManager_.current();

	renderer_->beginFrame(0.075f, 0.075f, 0.08f);
	beginEditorGuiFrame();
	if (current) {
		current->render();
	}
	renderEditorGuiFrame();
	if (auto* editor = dynamic_cast<EditorState*>(current); editor && !options_.editor.screenshotPath.empty()) {
		bool ok = true;
		for (const std::string& path : editor->takeScreenshotRequests()) {
			ok = captureScreenshot(path) && ok;
		}
		// Со сценарием работаем до его quit, без него — заданное число кадров.
		const bool scripted = !options_.editor.scriptPath.empty();
		const bool done = scripted ? editor->scriptFinished() : ++editorFrames_ >= options_.editor.screenshotFrames;
		if (!ok) {
			exitCode_ = 1;
		}
		if (done) {
			if (!captureScreenshot(options_.editor.screenshotPath)) {
				exitCode_ = 1;
			}
			if (auto* openGlRenderer = dynamic_cast<OpenGLRenderAdapter*>(renderer_.get())) {
				glfwSetWindowShouldClose(openGlRenderer->getWindow(), GLFW_TRUE);
			}
		}
	}
    {
        ZoneScopedN("Present");
        renderer_->endFrame();
    }
}

void Application::shutdown() {
	LOG_INFO("Application: shutting down");

	// Порядок важен: сначала запретить новые загрузки и свернуть начатые, потом дождаться задач,
	// и только затем чистить кэш и GL — задачи не должны пережить ни то, ни другое.
	ResourceManager::getInstance().beginShutdown();
	JobSystem::getInstance().shutdown();

	// Очищаем кэш ресурсов
	while (!stateManager_.isEmpty()) {
		stateManager_.pop();
	}
	ResourceManager::getInstance().clearCache();

	shutdownEditorGui();
	if (renderer_) {
		renderer_->shutdown();
	}
    InputManager::getInstance().shutdown();
    LOG_INFO("Shutdown complete");
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
	io.ConfigWindowsMoveFromTitleBarOnly = true;
	io.ConfigDragClickToInputText = true;
	// Раскладка окон и настройки редактора. Скриншоты всегда снимаются с раскладки по умолчанию.
	if (options_.editor.screenshotPath.empty()) {
		io.IniFilename = "editor_layout.ini";
	} else {
		io.IniFilename = options_.editor.iniPath.empty() ? nullptr : options_.editor.iniPath.c_str();
	}
	EditorState::registerSettingsHandler();

	EditorTheme::loadFonts();
	EditorTheme::apply(1.0f);

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
    ZoneScopedN("ImGui draw");
	if (!editorGuiInitialized_) {
		return;
	}

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool Application::captureScreenshot(const std::string& outputPath) {
	auto* openGlRenderer = dynamic_cast<OpenGLRenderAdapter*>(renderer_.get());
	std::vector<unsigned char> pixels;
	int width = 0;
	int height = 0;
	if (openGlRenderer == nullptr || !openGlRenderer->readCapturedFrame(pixels, width, height)) {
		LOG_ERROR("Application: failed to read the editor frame");
		return false;
	}
	const std::filesystem::path path(outputPath);
	std::error_code error;
	if (path.has_parent_path()) {
		std::filesystem::create_directories(path.parent_path(), error);
	}
	if (!stbi_write_png(path.string().c_str(), width, height, 4, pixels.data(), width * 4)) {
		LOG_ERROR("Application: failed to write " + path.string());
		return false;
	}
	LOG_INFO("Application: editor screenshot saved to " + path.string() + " (" + std::to_string(width) + "x" + std::to_string(height) + ")");
	return true;
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
