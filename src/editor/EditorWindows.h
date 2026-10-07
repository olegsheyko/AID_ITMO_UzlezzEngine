#pragma once

#include "editor/IconsLucide.h"

// Заголовки окон редактора. Всё после ### — стабильный ID: по нему док и imgui.ini узнают окно.
namespace EditorWindow {
constexpr const char* kHierarchy = ICON_LC_LIST_TREE "  Hierarchy###Hierarchy";
constexpr const char* kInspector = ICON_LC_SLIDERS_HORIZONTAL "  Inspector###Inspector";
constexpr const char* kScene = ICON_LC_BOX "  Scene###Scene";
constexpr const char* kGame = ICON_LC_GAMEPAD_2 "  Game###Game";
constexpr const char* kRendererInfo = ICON_LC_ACTIVITY "  Renderer Info###RendererInfo";
constexpr const char* kContentBrowser = ICON_LC_FOLDER "  Content Browser###ContentBrowser";
constexpr const char* kConsole = ICON_LC_SQUARE_TERMINAL "  Console###Console";
constexpr const char* kGameplay = ICON_LC_SWORDS "  Gameplay###Gameplay";
} // namespace EditorWindow
