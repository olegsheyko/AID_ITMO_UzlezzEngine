#pragma once
#include "render/IRenderAdapter.h"
#include "states/StateManager.h"
#include "core/Logger.h"
#include <memory>
#include <chrono>

class Application {
public:
    bool init(int width, int height, const char* title);
    void run();
    void shutdown();

private:
    bool initEditorGui();
    void beginEditorGuiFrame();
    void renderEditorGuiFrame();
    void shutdownEditorGui();
    void update(float dt);
    void render();

    std::unique_ptr<IRenderAdapter> renderer_;
    StateManager stateManager_;
    bool editorGuiInitialized_ = false;

    using Clock = std::chrono::high_resolution_clock;
    std::chrono::time_point<Clock> lastFrameTime_;
};
