#pragma once

#include "core/LaunchOptions.h"
#include "editor/EditorContext.h"
#include "editor/EditorScript.h"
#include "editor/panels/ConsolePanel.h"
#include "editor/panels/ContentBrowserPanel.h"
#include "editor/panels/GameViewPanel.h"
#include "editor/panels/GameplayPanel.h"
#include "editor/panels/HierarchyPanel.h"
#include "editor/panels/InspectorPanel.h"
#include "editor/panels/RendererInfoPanel.h"
#include "editor/panels/SceneViewPanel.h"
#include "editor/panels/ScriptEditorPanel.h"
#include "states/IGameState.h"

class IRenderAdapter;

// Окно редактора: меню, тулбар Play, статус-бар, док и панели поверх EditorContext.
class EditorState : public IGameState {
public:
    explicit EditorState(IRenderAdapter& renderer, EditorStartupOptions startup = {});

    // Регистрирует секцию настроек редактора в imgui.ini. Вызывать до первого NewFrame.
    static void registerSettingsHandler();

    void onEnter() override;
    void onExit() override;
    void update(float dt) override;
    void render() override;

    // Для Application: скриншоты, которые сценарий просит снять после этого кадра, и конец сценария.
    std::vector<std::string> takeScreenshotRequests();
    bool scriptFinished() const { return scriptQuit_; }

    // Закрытие окна при несохранённых Lua-скриптах: Application не закрывает окно, а просит редактор спросить,
    // и закрывает его, только когда quitConfirmed().
    bool hasUnsavedScripts() const { return scriptEditor_.hasUnsavedChanges(); }
    void askToQuit() { quitDialogRequested_ = true; }
    bool quitConfirmed() const { return quitConfirmed_; }

private:
    void renderMainMenu();
    void renderMainToolbar();
    void renderStatusBar();
    void renderDockSpace();
    void buildDefaultLayout(unsigned int dockspaceId);
    void renderModals();
    void handleShortcuts();
    void applyStartupOptions();
    void loadPreferences();
    void storePreferences();
    void togglePlay();
    void togglePause();
    void step();

    EditorContext context_;
    HierarchyPanel hierarchy_;
    InspectorPanel inspector_;
    SceneViewPanel sceneView_;
    GameViewPanel gameView_;
    RendererInfoPanel rendererInfo_;
    GameplayPanel gameplay_;
    ScriptEditorPanel scriptEditor_;
    ContentBrowserPanel contentBrowser_;
    ConsolePanel console_;
    EditorStartupOptions startup_;
    EditorScript script_;
    bool scriptQuit_ = false;
    std::vector<std::string> screenshotRequests_;

    float uiScale_ = 1.0f;
    float pendingUiScale_ = 0.0f;
    float lastDt_ = 0.0f;
    bool resetLayout_ = false;
    bool showControls_ = false;
    bool showAbout_ = false;
    bool showImGuiDemo_ = false;
    bool quitDialogRequested_ = false;
    bool quitConfirmed_ = false;
    bool quitSaveFailed_ = false;
    int frame_ = 0;
};
