#include "states/EditorState.h"

#include "core/Logger.h"
#include "editor/EditorIcons.h"
#include "editor/EditorTheme.h"
#include "editor/EditorWidgets.h"
#include "editor/EditorWindows.h"
#include "editor/IconsLucide.h"
#include "editor/ThumbnailCache.h"
#include "resources/ResourceManager.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <string>

using namespace EditorTheme;

namespace {
// Растёт, когда меняется раскладка по умолчанию: старый imgui.ini тогда собирается заново.
constexpr int kLayoutVersion = 3;

// Настройки редактора в imgui.ini, секция [UzlezzEditor][Preferences].
// Хранятся статически: ImGui пишет ini и после того, как состояние редактора уже уничтожено.
std::map<std::string, std::string>& preferences() {
    static std::map<std::string, std::string> values;
    return values;
}

float prefFloat(const char* key, float fallback) {
    auto it = preferences().find(key);
    if (it == preferences().end()) {
        return fallback;
    }
    return std::strtof(it->second.c_str(), nullptr);
}

bool prefBool(const char* key, bool fallback) {
    auto it = preferences().find(key);
    return it == preferences().end() ? fallback : it->second == "1";
}

const char* windowForKey(const std::string& key) {
    if (key == "scene") return EditorWindow::kScene;
    if (key == "game") return EditorWindow::kGame;
    if (key == "hierarchy") return EditorWindow::kHierarchy;
    if (key == "inspector") return EditorWindow::kInspector;
    if (key == "content") return EditorWindow::kContentBrowser;
    if (key == "console") return EditorWindow::kConsole;
    if (key == "renderer") return EditorWindow::kRendererInfo;
    return nullptr;
}

// Кнопка сегмента в группе Play / Pause / Step.
bool playButton(const char* id, const char* icon, const char* tooltip, bool active, ImU32 activeColor, bool enabled, ImDrawFlags corners) {
    const ImVec2 size(38.0f, ImGui::GetFrameHeight() + 2.0f);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(id, size);
    ImGui::EndDisabled();
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && enabled;
    const bool held = ImGui::IsItemActive();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 background = kFrame;
    if (active) {
        background = held ? mix(activeColor, IM_COL32_BLACK, 0.15f) : (hovered ? mix(activeColor, IM_COL32_WHITE, 0.12f) : activeColor);
    } else if (held) {
        background = kFrameActive;
    } else if (hovered) {
        background = kFrameHovered;
    }
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(background), 6.0f, corners);
    const ImU32 iconColor = !enabled ? kTextFaint : (active ? IM_COL32_WHITE : (hovered ? kText : IM_COL32(200, 200, 204, 255)));
    EditorUI::drawTextCentered(drawList, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f), icon, iconColor);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return clicked && enabled;
}
}

void EditorState::registerSettingsHandler() {
    if (ImGui::FindSettingsHandler("UzlezzEditor")) {
        return;
    }
    ImGuiSettingsHandler handler;
    handler.TypeName = "UzlezzEditor";
    handler.TypeHash = ImHashStr("UzlezzEditor");
    handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char* name) -> void* {
        return std::strcmp(name, "Preferences") == 0 ? &preferences() : nullptr;
    };
    handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line) {
        const char* separator = std::strchr(line, '=');
        if (separator != nullptr && separator != line) {
            preferences()[std::string(line, separator)] = std::string(separator + 1);
        }
    };
    handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* self, ImGuiTextBuffer* buffer) {
        if (preferences().empty()) {
            return;
        }
        buffer->appendf("[%s][Preferences]\n", self->TypeName);
        for (const auto& [key, value] : preferences()) {
            buffer->appendf("%s=%s\n", key.c_str(), value.c_str());
        }
        buffer->append("\n");
    };
    ImGui::AddSettingsHandler(&handler);
}

EditorState::EditorState(IRenderAdapter& renderer, EditorStartupOptions startup)
    : context_(renderer),
      startup_(std::move(startup)) {
}

void EditorState::onEnter() {
    LOG_INFO("EditorState: entered");
    if (!startup_.scriptPath.empty()) {
        std::string error;
        if (!script_.load(startup_.scriptPath, error)) {
            LOG_ERROR("EditorState: " + error);
        }
    }
    context_.enter();
    loadPreferences();
    if (prefFloat("LayoutVersion", 0.0f) != static_cast<float>(kLayoutVersion) || startup_.resetLayout) {
        resetLayout_ = true;
    }
    EditorTheme::apply(uiScale_);
}

void EditorState::onExit() {
    LOG_INFO("EditorState: exited");
    storePreferences();
    context_.exit();
}

void EditorState::loadPreferences() {
    uiScale_ = std::clamp(prefFloat("UiScale", 1.0f), 0.75f, 2.0f);
    contentBrowser_.tileSize = std::clamp(prefFloat("TileSize", contentBrowser_.tileSize), 56.0f, 168.0f);
    contentBrowser_.listView = prefBool("ListView", false) || startup_.listView;
    console_.collapse = prefBool("ConsoleCollapse", false);
    console_.autoScroll = prefBool("ConsoleAutoScroll", true);
    console_.showInfo = prefBool("ConsoleInfo", true);
    console_.showWarnings = prefBool("ConsoleWarnings", true);
    console_.showErrors = prefBool("ConsoleErrors", true);
    SceneViewSettings& scene = context_.sceneView;
    scene.showGrid = prefBool("Grid", true);
    scene.snap = prefBool("Snap", false);
    scene.translateSnap = prefFloat("SnapMove", scene.translateSnap);
    scene.rotateSnapDegrees = prefFloat("SnapRotate", scene.rotateSnapDegrees);
    scene.scaleSnap = prefFloat("SnapScale", scene.scaleSnap);
    scene.localSpace = prefBool("LocalSpace", true);
    scene.showSelectionOutline = prefBool("SelectionOutline", true);
    scene.showCameraIcons = prefBool("CameraIcons", true);
    scene.showStats = prefBool("SceneStats", false);
    context_.debugRenderSystem.setEnabled(prefBool("Colliders", false) || startup_.showColliders);
    context_.camera.moveSpeed = std::clamp(prefFloat("CameraSpeed", context_.camera.moveSpeed), 0.5f, 40.0f);
    gameView_.resolution = static_cast<int>(prefFloat("GameResolution", 0.0f));
    gameView_.showStats = prefBool("GameStats", false);
    hierarchy_.open = prefBool("ShowHierarchy", true);
    inspector_.open = prefBool("ShowInspector", true);
    sceneView_.open = prefBool("ShowScene", true);
    gameView_.open = prefBool("ShowGame", true);
    rendererInfo_.open = prefBool("ShowRendererInfo", true);
    contentBrowser_.open = prefBool("ShowContentBrowser", true);
    console_.open = prefBool("ShowConsole", true);
}

void EditorState::storePreferences() {
    std::map<std::string, std::string> values;
    auto setFloat = [&values](const char* key, float value) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%g", value);
        values[key] = buffer;
    };
    auto setBool = [&values](const char* key, bool value) { values[key] = value ? "1" : "0"; };
    const SceneViewSettings& scene = context_.sceneView;
    setFloat("LayoutVersion", static_cast<float>(kLayoutVersion));
    setFloat("UiScale", uiScale_);
    setFloat("TileSize", contentBrowser_.tileSize);
    setBool("ListView", contentBrowser_.listView);
    setBool("ConsoleCollapse", console_.collapse);
    setBool("ConsoleAutoScroll", console_.autoScroll);
    setBool("ConsoleInfo", console_.showInfo);
    setBool("ConsoleWarnings", console_.showWarnings);
    setBool("ConsoleErrors", console_.showErrors);
    setBool("Grid", scene.showGrid);
    setBool("Snap", scene.snap);
    setFloat("SnapMove", scene.translateSnap);
    setFloat("SnapRotate", scene.rotateSnapDegrees);
    setFloat("SnapScale", scene.scaleSnap);
    setBool("LocalSpace", scene.localSpace);
    setBool("SelectionOutline", scene.showSelectionOutline);
    setBool("CameraIcons", scene.showCameraIcons);
    setBool("SceneStats", scene.showStats);
    setBool("Colliders", context_.debugRenderSystem.isEnabled());
    setFloat("CameraSpeed", context_.camera.moveSpeed);
    setFloat("GameResolution", static_cast<float>(gameView_.resolution));
    setBool("GameStats", gameView_.showStats);
    setBool("ShowHierarchy", hierarchy_.open);
    setBool("ShowInspector", inspector_.open);
    setBool("ShowScene", sceneView_.open);
    setBool("ShowGame", gameView_.open);
    setBool("ShowRendererInfo", rendererInfo_.open);
    setBool("ShowContentBrowser", contentBrowser_.open);
    setBool("ShowConsole", console_.open);
    if (values != preferences()) {
        preferences() = std::move(values);
        ImGui::MarkIniSettingsDirty();
    }
}

void EditorState::update(float dt) {
    ZoneScoped;
    lastDt_ = dt;
    // Стиль можно менять только между кадрами ImGui — update() как раз идёт до NewFrame().
    if (pendingUiScale_ > 0.0f) {
        uiScale_ = pendingUiScale_;
        pendingUiScale_ = 0.0f;
        EditorTheme::apply(uiScale_);
    }
    if (!script_.empty()) {
        script_.apply(frame_);
    }
    context_.update(dt);
}

std::vector<std::string> EditorState::takeScreenshotRequests() {
    std::vector<std::string> requests;
    requests.swap(screenshotRequests_);
    return requests;
}

void EditorState::togglePlay() {
    if (context_.isPlaying()) {
        context_.stop();
        if (sceneView_.open) {
            ImGui::SetWindowFocus(EditorWindow::kScene);
        }
    } else {
        context_.play();
        if (gameView_.open) {
            ImGui::SetWindowFocus(EditorWindow::kGame);
        }
    }
}

void EditorState::togglePause() {
    context_.setPaused(!context_.paused);
}

void EditorState::step() {
    context_.stepFrame();
}

void EditorState::render() {
    ZoneScopedN("Editor UI");
    console_.poll();
    handleShortcuts();
    renderMainMenu();
    renderMainToolbar();
    renderStatusBar();
    renderDockSpace();

    hierarchy_.draw(context_);
    sceneView_.draw(context_);
    gameView_.draw(context_);
    rendererInfo_.draw(context_);
    contentBrowser_.draw(context_, lastDt_);
    console_.draw();
    inspector_.draw(context_);
    renderModals();
    if (showImGuiDemo_) {
        ImGui::ShowDemoWindow(&showImGuiDemo_);
    }

    if (!context_.focusWindowRequest.empty()) {
        ImGui::SetWindowFocus(context_.focusWindowRequest.c_str());
        context_.focusWindowRequest.clear();
    }
    if (frame_ == 2) {
        applyStartupOptions();
    }
    storePreferences();
    if (!script_.empty()) {
        for (const std::string& path : script_.screenshots(frame_)) {
            screenshotRequests_.push_back(path);
        }
        scriptQuit_ = scriptQuit_ || script_.quitAt(frame_);
    }
    ++frame_;
}

void EditorState::applyStartupOptions() {
    if (!startup_.selectEntity.empty()) {
        for (Entity entity : context_.world.getEntities()) {
            if (context_.isEditable(entity) && context_.displayName(entity) == startup_.selectEntity) {
                context_.select(entity);
                context_.focusSelection();
                break;
            }
        }
    }
    if (!startup_.browseFolder.empty()) {
        contentBrowser_.openFolder(context_, startup_.browseFolder);
    }
    if (!startup_.selectAsset.empty()) {
        context_.revealAssetRequest = startup_.selectAsset;
    }
    for (const std::string& key : startup_.focusWindows) {
        if (const char* window = windowForKey(key)) {
            ImGui::SetWindowFocus(window);
        }
    }
    if (startup_.play && !context_.isPlaying()) {
        togglePlay();
    }
}

void EditorState::handleShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    const ImGuiInputFlags global = ImGuiInputFlags_RouteGlobal;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_P, global)) {
        togglePlay();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, global)) {
        togglePause();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_P, global)) {
        step();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_R, global)) {
        context_.assets.refresh();
        ThumbnailCache::instance().forgetFailures();
    }

    const bool typing = io.WantTextInput || ImGui::IsAnyItemActive();
    if (typing || context_.gameViewInputActive) {
        return;
    }
    const bool editable = !context_.isPlaying() && context_.isEditable(context_.selected);
    if (editable && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D, global)) {
        context_.duplicate(context_.selected);
    }
    if (editable && ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        context_.beginRename(context_.selected);
    }
    if (editable && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Backspace, global))) {
        context_.destroy(context_.selected);
    }
    if (context_.isEditable(context_.selected) && !io.KeyCtrl && !io.KeySuper && !io.KeyAlt &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Right) && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        context_.focusSelection();
    }
}

void EditorState::renderMainMenu() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, toVec4(kBackground));
    const bool open = ImGui::BeginMainMenuBar();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (!open) {
        return;
    }
    const bool playing = context_.isPlaying();
    const bool editable = !playing && context_.isEditable(context_.selected);

    if (ImGui::BeginMenu("File")) {
        if (ImGui::BeginMenu(ICON_LC_CLAPPERBOARD "  Open Scene")) {
            int scenes = 0;
            for (const AssetEntry& entry : context_.assets.search(".json", 200)) {
                if (entry.type != AssetType::Scene) {
                    continue;
                }
                ++scenes;
                if (ImGui::MenuItem(entry.name.c_str(), nullptr, entry.path == context_.scenePath())) {
                    context_.loadScene(entry.path);
                }
            }
            if (scenes == 0) {
                ImGui::MenuItem("No scenes in assets/scenes", nullptr, false, false);
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(ICON_LC_ROTATE_CCW "  Reload Scene")) {
            context_.reloadScene();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_LC_REFRESH_CW "  Refresh Assets", EditorUI::shortcut("Ctrl+R").c_str())) {
            context_.assets.refresh();
            ThumbnailCache::instance().forgetFailures();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem(ICON_LC_COPY "  Duplicate", EditorUI::shortcut("Ctrl+D").c_str(), false, editable)) {
            context_.duplicate(context_.selected);
        }
        if (ImGui::MenuItem(ICON_LC_PENCIL "  Rename", "F2", false, editable)) {
            context_.beginRename(context_.selected);
        }
        if (ImGui::MenuItem(ICON_LC_TRASH_2 "  Delete", "Del", false, editable)) {
            context_.destroy(context_.selected);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_LC_FOCUS "  Frame Selected", "F", false, context_.isEditable(context_.selected))) {
            context_.focusSelection();
        }
        if (ImGui::MenuItem(ICON_LC_X "  Deselect", nullptr, false, context_.selected != kInvalidEntity || !context_.selectedAsset.empty())) {
            context_.clearSelection();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Entity")) {
        ImGui::BeginDisabled(playing);
        if (ImGui::MenuItem(ICON_LC_SQUARE_DASHED "  Create Empty")) {
            context_.beginRename(context_.createEmpty("Empty", context_.defaultSpawnPosition()));
        }
        if (ImGui::MenuItem(ICON_LC_BOX "  Cube")) {
            context_.createCube("Cube", context_.defaultSpawnPosition());
        }
        if (ImGui::BeginMenu(ICON_LC_PACKAGE "  Model")) {
            int models = 0;
            for (const AssetEntry& entry : context_.assets.search(".", 2000)) {
                if (entry.type != AssetType::Model) {
                    continue;
                }
                ++models;
                if (ImGui::MenuItem(entry.name.c_str())) {
                    context_.createModel(entry.path, context_.dropPoint(context_.camera.getPosition(), context_.camera.getForward()));
                }
            }
            if (models == 0) {
                ImGui::MenuItem("No models in assets", nullptr, false, false);
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_LC_PERSON_STANDING "  Animated Crowd...")) {
            rendererInfo_.open = true;
            context_.focusWindowRequest = EditorWindow::kRendererInfo;
        }
        ImGui::EndDisabled();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem(ICON_LC_LIST_TREE "  Hierarchy", nullptr, &hierarchy_.open);
        ImGui::MenuItem(ICON_LC_SLIDERS_HORIZONTAL "  Inspector", nullptr, &inspector_.open);
        ImGui::MenuItem(ICON_LC_BOX "  Scene", nullptr, &sceneView_.open);
        ImGui::MenuItem(ICON_LC_GAMEPAD_2 "  Game", nullptr, &gameView_.open);
        ImGui::MenuItem(ICON_LC_ACTIVITY "  Renderer Info", nullptr, &rendererInfo_.open);
        ImGui::MenuItem(ICON_LC_FOLDER "  Content Browser", nullptr, &contentBrowser_.open);
        ImGui::MenuItem(ICON_LC_SQUARE_TERMINAL "  Console", nullptr, &console_.open);
        ImGui::Separator();
        if (ImGui::BeginMenu(ICON_LC_TYPE "  UI Scale")) {
            const float scales[] = {0.9f, 1.0f, 1.1f, 1.25f, 1.5f};
            for (float scale : scales) {
                char label[16];
                std::snprintf(label, sizeof(label), "%.0f%%", scale * 100.0f);
                if (ImGui::MenuItem(label, nullptr, std::abs(uiScale_ - scale) < 0.01f)) {
                    pendingUiScale_ = scale;
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(ICON_LC_LAYOUT_DASHBOARD "  Reset Layout")) {
            resetLayout_ = true;
        }
        ImGui::Separator();
        ImGui::MenuItem(ICON_LC_GRID_3X3 "  Grid", nullptr, &context_.sceneView.showGrid);
        bool colliders = context_.debugRenderSystem.isEnabled();
        if (ImGui::MenuItem(ICON_LC_SQUARE_DASHED "  Colliders", nullptr, &colliders)) {
            context_.debugRenderSystem.setEnabled(colliders);
        }
        ImGui::MenuItem(ICON_LC_SCAN "  Selection Outline", nullptr, &context_.sceneView.showSelectionOutline);
        ImGui::Separator();
        ImGui::MenuItem(ICON_LC_APP_WINDOW "  ImGui Demo", nullptr, &showImGuiDemo_);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Play")) {
        if (ImGui::MenuItem(playing ? ICON_LC_SQUARE "  Stop" : ICON_LC_PLAY "  Play", EditorUI::shortcut("Ctrl+P").c_str())) {
            togglePlay();
        }
        if (ImGui::MenuItem(ICON_LC_PAUSE "  Pause", EditorUI::shortcut("Ctrl+Shift+P").c_str(), context_.paused)) {
            togglePause();
        }
        if (ImGui::MenuItem(ICON_LC_STEP_FORWARD "  Step", EditorUI::shortcut("Ctrl+Alt+P").c_str(), false, playing)) {
            step();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem(ICON_LC_KEYBOARD "  Controls")) {
            showControls_ = true;
        }
        if (ImGui::MenuItem(ICON_LC_INFO "  About")) {
            showAbout_ = true;
        }
        ImGui::EndMenu();
    }

    // Справа — название проекта и сцены, как в заголовке Unity.
    const std::string title = std::string("UzlezzEngine  \xC2\xB7  ") + context_.sceneName();
    const float width = ImGui::CalcTextSize(title.c_str()).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - width - 16.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextFaint));
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopStyleColor();
    ImGui::EndMainMenuBar();
}

void EditorState::renderMainToolbar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float height = ImGui::GetFrameHeight() + 14.0f;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, toVec4(kBackground));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
    if (ImGui::BeginViewportSideBar("##MainToolbar", viewport, ImGuiDir_Up, height, flags)) {
        const bool playing = context_.isPlaying();
        // Слева — сцена.
        ImGui::SetCursorPos(ImVec2(12.0f, 6.0f));
        ImGui::AlignTextToFramePadding();
        EditorUI::iconLabel(ICON_LC_CLAPPERBOARD, context_.sceneName().c_str(), kAssetScene, kTextDim);

        // По центру — Play / Pause / Step.
        const float groupWidth = 38.0f * 3.0f + 2.0f;
        ImGui::SetCursorPos(ImVec2((ImGui::GetWindowWidth() - groupWidth) * 0.5f, 5.0f));
        if (playButton("##play", playing ? ICON_LC_SQUARE : ICON_LC_PLAY, playing ? "Stop (Ctrl+P)" : "Play (Ctrl+P)",
                playing, kPlay, true, ImDrawFlags_RoundCornersLeft)) {
            togglePlay();
        }
        ImGui::SameLine(0.0f, 1.0f);
        if (playButton("##pause", ICON_LC_PAUSE, "Pause (Ctrl+Shift+P)", context_.paused, IM_COL32(214, 150, 40, 255), true,
                ImDrawFlags_RoundCornersNone)) {
            togglePause();
        }
        ImGui::SameLine(0.0f, 1.0f);
        if (playButton("##step", ICON_LC_STEP_FORWARD, "Step one frame (Ctrl+Alt+P)", false, kPlay, playing, ImDrawFlags_RoundCornersRight)) {
            step();
        }

        // Справа — раскладка.
        const float rightWidth = 120.0f;
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - rightWidth - 8.0f, 5.0f));
        if (EditorUI::toolButton("##layout", ICON_LC_LAYOUT_DASHBOARD, "Layout", false, "Window layout")) {
            ImGui::OpenPopup("##layout_popup");
        }
        if (ImGui::BeginPopup("##layout_popup")) {
            if (ImGui::MenuItem(ICON_LC_LAYOUT_DASHBOARD "  Reset Layout")) {
                resetLayout_ = true;
            }
            ImGui::Separator();
            ImGui::MenuItem("Hierarchy", nullptr, &hierarchy_.open);
            ImGui::MenuItem("Inspector", nullptr, &inspector_.open);
            ImGui::MenuItem("Scene", nullptr, &sceneView_.open);
            ImGui::MenuItem("Game", nullptr, &gameView_.open);
            ImGui::MenuItem("Renderer Info", nullptr, &rendererInfo_.open);
            ImGui::MenuItem("Content Browser", nullptr, &contentBrowser_.open);
            ImGui::MenuItem("Console", nullptr, &console_.open);
            ImGui::EndPopup();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void EditorState::renderStatusBar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float height = ImGui::GetTextLineHeight() + 10.0f;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, toVec4(context_.isPlaying() ? IM_COL32(26, 46, 82, 255) : kBackground));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 4.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
    if (ImGui::BeginViewportSideBar("##StatusBar", viewport, ImGuiDir_Down, height, flags)) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 windowPos = ImGui::GetWindowPos();
        const float windowWidth = ImGui::GetWindowWidth();
        const float textY = windowPos.y + (height - ImGui::GetFontSize()) * 0.5f;
        EditorUI::pushSmallFont();
        const float smallY = windowPos.y + (height - ImGui::GetFontSize()) * 0.5f;

        // Справа налево: FPS, сущности, загрузки, режим.
        std::vector<std::pair<std::string, ImU32>> items;
        char fps[32];
        std::snprintf(fps, sizeof(fps), "%.0f FPS  %.1f ms", context_.fpsAverage, context_.lastDt * 1000.0f);
        items.emplace_back(fps, kTextDim);
        const std::size_t entities = context_.world.getEntityCount() - (context_.world.isAlive(context_.editorCameraEntity) ? 1u : 0u);
        items.emplace_back(std::string(ICON_LC_BOXES "  ") + std::to_string(entities) + " entities", kTextDim);
        const std::size_t pending = ResourceManager::getInstance().pendingLoadCount();
        if (pending > 0) {
            items.emplace_back(std::string(ICON_LC_LOADER_CIRCLE "  Loading ") + std::to_string(pending), kAccentHovered);
        }
        if (context_.isPlaying()) {
            items.emplace_back(context_.paused ? ICON_LC_PAUSE "  Paused" : ICON_LC_PLAY "  Playing", context_.paused ? kWarning : IM_COL32(150, 196, 255, 255));
        } else {
            items.emplace_back(ICON_LC_PENCIL "  Edit Mode", kTextFaint);
        }
        float x = windowPos.x + windowWidth - 12.0f;
        for (const auto& [text, color] : items) {
            const float width = ImGui::CalcTextSize(text.c_str()).x;
            x -= width;
            drawList->AddText(ImVec2(x, smallY), ImGui::GetColorU32(color), text.c_str());
            x -= 22.0f;
            drawList->AddLine(ImVec2(x + 11.0f, windowPos.y + 6.0f), ImVec2(x + 11.0f, windowPos.y + height - 6.0f), ImGui::GetColorU32(kBorderStrong));
        }

        // Слева — последнее сообщение лога; клик открывает консоль.
        if (const Logger::Entry* latest = console_.latest()) {
            const ImU32 color = latest->level == Logger::Level::ERROR ? kError : latest->level == Logger::Level::WARN ? kWarning : kTextDim;
            const char* icon = latest->level == Logger::Level::ERROR ? ICON_LC_OCTAGON_ALERT
                : latest->level == Logger::Level::WARN ? ICON_LC_TRIANGLE_ALERT : ICON_LC_INFO;
            const float left = windowPos.x + 12.0f;
            const float maxWidth = x - left - 12.0f;
            ImGui::SetCursorScreenPos(ImVec2(left - 4.0f, windowPos.y));
            if (maxWidth > 40.0f && ImGui::InvisibleButton("##last_log", ImVec2(maxWidth, height))) {
                console_.open = true;
                context_.focusWindowRequest = EditorWindow::kConsole;
            }
            const bool hovered = ImGui::IsItemHovered();
            drawList->AddText(ImVec2(left, smallY), ImGui::GetColorU32(color), icon);
            const std::string message = latest->message.substr(0, latest->message.find('\n'));
            EditorUI::drawTextEllipsis(drawList, ImVec2(left + 20.0f, smallY), maxWidth - 24.0f, message.c_str(), hovered ? kText : color);
        }
        (void)textY;
        EditorUI::popFont();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void EditorState::renderDockSpace() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");
    if (resetLayout_ || ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
        buildDefaultLayout(dockspaceId);
        resetLayout_ = false;
    }
    ImGui::PushStyleColor(ImGuiCol_WindowBg, toVec4(kBackground));
    ImGui::DockSpaceOverViewport(dockspaceId, viewport, ImGuiDockNodeFlags_None);
    ImGui::PopStyleColor();
}

void EditorState::buildDefaultLayout(unsigned int dockspaceId) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

    ImGuiID root = dockspaceId;
    ImGuiID left = 0;
    ImGuiID right = 0;
    ImGuiID bottom = 0;
    ImGuiID center = 0;
    ImGui::DockBuilderSplitNode(root, ImGuiDir_Left, 0.17f, &left, &root);
    ImGui::DockBuilderSplitNode(root, ImGuiDir_Right, 0.27f, &right, &root);
    ImGui::DockBuilderSplitNode(root, ImGuiDir_Down, 0.34f, &bottom, &center);

    ImGui::DockBuilderDockWindow(EditorWindow::kHierarchy, left);
    ImGui::DockBuilderDockWindow(EditorWindow::kInspector, right);
    ImGui::DockBuilderDockWindow(EditorWindow::kScene, center);
    ImGui::DockBuilderDockWindow(EditorWindow::kGame, center);
    ImGui::DockBuilderDockWindow(EditorWindow::kRendererInfo, center);
    ImGui::DockBuilderDockWindow(EditorWindow::kContentBrowser, bottom);
    ImGui::DockBuilderDockWindow(EditorWindow::kConsole, bottom);
    ImGui::DockBuilderFinish(dockspaceId);
    if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(center)) {
        node->SelectedTabId = ImHashStr(EditorWindow::kScene);
    }
    if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(bottom)) {
        node->SelectedTabId = ImHashStr(EditorWindow::kContentBrowser);
    }
    hierarchy_.open = inspector_.open = sceneView_.open = gameView_.open = true;
    rendererInfo_.open = contentBrowser_.open = console_.open = true;
}

void EditorState::renderModals() {
    if (showControls_) {
        ImGui::OpenPopup("Controls##modal");
        showControls_ = false;
    }
    if (showAbout_) {
        ImGui::OpenPopup("About##modal");
        showAbout_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Controls##modal", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        struct Row {
            const char* keys;
            const char* action;
        };
        const Row sections[][9] = {
            {{"RMB + WASD", "Fly the scene camera (Q/E down/up, Shift faster)"}, {"Alt + LMB", "Orbit around the pivot"},
             {"MMB", "Pan"}, {"Wheel", "Zoom"}, {"F", "Frame selected"}, {"Axis gizmo", "Click an axis to look along it"},
             {nullptr, nullptr}},
            {{"Q / W / E / R", "Select / Move / Rotate / Scale"}, {"X", "Toggle local / global handles"},
             {"Ctrl (while dragging)", "Invert snapping"}, {"Ctrl+D", "Duplicate"}, {"F2", "Rename"}, {"Del", "Delete"},
             {nullptr, nullptr}},
            {{"Ctrl+P", "Play / Stop"}, {"Ctrl+Shift+P", "Pause"}, {"Ctrl+Alt+P", "Step one frame"},
             {"Arrows / WASD", "Move the player cube (Game view focused)"}, {"Space", "Jump"}, {"Ctrl+R", "Refresh assets"},
             {nullptr, nullptr}},
        };
        const char* titles[] = {"Scene camera", "Editing", "Play mode"};
        for (int section = 0; section < 3; ++section) {
            EditorUI::pushSemibold();
            ImGui::TextUnformatted(titles[section]);
            EditorUI::popFont();
            if (ImGui::BeginTable(titles[section], 2, ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("keys", ImGuiTableColumnFlags_WidthFixed, 170.0f);
                ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch);
                for (const Row& row : sections[section]) {
                    if (row.keys == nullptr) {
                        break;
                    }
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    const std::string keys = EditorUI::shortcut(row.keys);
                    ImGui::TextColored(toVec4(kAccentHovered), "%s", keys.c_str());
                    ImGui::TableSetColumnIndex(1);
                    EditorUI::textDim(row.action);
                }
                ImGui::EndTable();
            }
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
        }
        ImGui::Separator();
        if (EditorUI::primaryButton("Got it", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("About##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::PushFont(fonts().semibold, 20.0f);
        ImGui::TextUnformatted(ICON_LC_BOX "  UzlezzEngine");
        ImGui::PopFont();
        EditorUI::textDim("Educational game engine, ITMO University");
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        EditorUI::textFaint("OpenGL 3.3  \xC2\xB7  Dear ImGui " IMGUI_VERSION "  \xC2\xB7  ImGuizmo  \xC2\xB7  enkiTS  \xC2\xB7  Assimp  \xC2\xB7  Tracy");
        EditorUI::textFaint("Fonts: Inter, JetBrains Mono (OFL)  \xC2\xB7  Icons: Lucide (ISC)");
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        if (EditorUI::primaryButton("Close", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
