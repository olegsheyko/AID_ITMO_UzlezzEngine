#include "render/OpenGLRenderAdapter.h"
#include "core/Logger.h"

OpenGLRenderAdapter::~OpenGLRenderAdapter() {
	shutdown();
}

bool OpenGLRenderAdapter::init(int width, int height, const std::string& title) {
	shutdown();

	if (!glfwInit()) {
		LOG_ERROR("OpenGLRenderAdapter: Failed to initialize GLFW");
		return false;
	}
	initialized_ = true;

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
	if (!window_) {
		LOG_ERROR("OpenGLRenderAdapter: Failed to create GLFW window");
		shutdown();
		return false;
	}

	glfwMakeContextCurrent(window_);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		LOG_ERROR("OpenGLRenderAdapter: Failed to initialize GLAD");
		shutdown();
		return false;
	}

	LOG_INFO("OpenGLRenderAdapter: Successfully initialized OpenGL context");
	return true;
}

bool OpenGLRenderAdapter::isRunning() const {
	return window_ != nullptr && !glfwWindowShouldClose(window_);
}

void OpenGLRenderAdapter::pollEvents() {
	if (initialized_) {
		glfwPollEvents();
	}
}

void OpenGLRenderAdapter::beginFrame(float r, float g, float b) {
	glClearColor(r, g, b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRenderAdapter::endFrame() {
	if (window_) {
		glfwSwapBuffers(window_);
	}
}

void OpenGLRenderAdapter::shutdown() {
	bool released = false;

	if (window_) {
		glfwDestroyWindow(window_);
		window_ = nullptr;
		released = true;
	}
	if (initialized_) {
		glfwTerminate();
		initialized_ = false;
		released = true;
	}

	if (released) {
		LOG_INFO("OpenGLRenderAdapter: Shutdown complete");
	}
}
