#pragma once
#include "IRenderAdapter.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

class OpenGLRenderAdapter : public IRenderAdapter {
	public:
	virtual ~OpenGLRenderAdapter() override;
	virtual bool init(int width, int height, const std::string& title) override;
	virtual bool isRunning() const override;
	virtual void pollEvents() override;
	virtual void beginFrame(float r, float g, float b) override;
	virtual void endFrame() override;
	virtual void shutdown() override;

	GLFWwindow* getWindow() const { return window_; }

private:
	GLFWwindow* window_ = nullptr;
	bool initialized_ = false;
};
