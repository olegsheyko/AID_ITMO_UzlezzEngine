#pragma once
#include <string>

class IRenderAdapter {
	public:
	virtual ~IRenderAdapter() = default;

	// Create a window and initialize the renderer.
	virtual bool init(int width, int height, const std::string& title) = 0;

	// Check whether the window should keep running.
	virtual bool isRunning() const = 0;

	// Process platform events.
	virtual void pollEvents() = 0;

	// Clear the framebuffer and prepare for drawing.
	virtual void beginFrame(float r, float g, float b) = 0;

	// Present the rendered frame.
	virtual void endFrame() = 0;

	// Release window and renderer resources.
	virtual void shutdown() = 0;
};
