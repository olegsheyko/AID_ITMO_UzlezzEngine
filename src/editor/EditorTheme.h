#pragma once

#include <imgui.h>

// Внешний вид редактора: палитра, метрики и шрифты. Цвета — только отсюда, чтобы тему можно было крутить в одном месте.
namespace EditorTheme {
// Базовые тона — нейтральный графит.
constexpr ImU32 kBackground = IM_COL32(19, 19, 20, 255);       // щели между панелями, фон дока
constexpr ImU32 kPanel = IM_COL32(30, 30, 31, 255);            // окна
constexpr ImU32 kPanelRaised = IM_COL32(37, 37, 39, 255);      // заголовки карточек, тулбары
constexpr ImU32 kFrame = IM_COL32(44, 44, 46, 255);            // поля ввода
constexpr ImU32 kFrameHovered = IM_COL32(52, 52, 55, 255);
constexpr ImU32 kFrameActive = IM_COL32(60, 60, 64, 255);
constexpr ImU32 kBorder = IM_COL32(48, 48, 51, 255);
constexpr ImU32 kBorderStrong = IM_COL32(62, 62, 66, 255);
constexpr ImU32 kText = IM_COL32(226, 226, 228, 255);
constexpr ImU32 kTextDim = IM_COL32(146, 146, 152, 255);
constexpr ImU32 kTextFaint = IM_COL32(98, 98, 104, 255);

// Акценты
constexpr ImU32 kAccent = IM_COL32(61, 139, 253, 255);
constexpr ImU32 kAccentHovered = IM_COL32(92, 159, 255, 255);
constexpr ImU32 kAccentSoft = IM_COL32(61, 139, 253, 56);
constexpr ImU32 kAccentSofter = IM_COL32(61, 139, 253, 28);
constexpr ImU32 kSelection = IM_COL32(255, 154, 46, 255);      // выделение в сцене, как в Unity
constexpr ImU32 kWarning = IM_COL32(236, 172, 62, 255);
constexpr ImU32 kError = IM_COL32(240, 86, 78, 255);
constexpr ImU32 kSuccess = IM_COL32(70, 196, 128, 255);
constexpr ImU32 kPlay = IM_COL32(61, 139, 253, 255);

// Оси
constexpr ImU32 kAxisX = IM_COL32(230, 76, 82, 255);
constexpr ImU32 kAxisY = IM_COL32(118, 196, 72, 255);
constexpr ImU32 kAxisZ = IM_COL32(64, 136, 246, 255);

// Цвета типов ассетов
constexpr ImU32 kFolder = IM_COL32(232, 184, 76, 255);
constexpr ImU32 kAssetModel = IM_COL32(242, 140, 64, 255);
constexpr ImU32 kAssetShader = IM_COL32(170, 120, 250, 255);
constexpr ImU32 kAssetScene = IM_COL32(72, 196, 140, 255);
constexpr ImU32 kAssetMaterial = IM_COL32(236, 102, 160, 255);
constexpr ImU32 kAssetText = IM_COL32(120, 160, 200, 255);
constexpr ImU32 kAssetFont = IM_COL32(200, 200, 120, 255);
constexpr ImU32 kAssetAudio = IM_COL32(226, 96, 196, 255);
constexpr ImU32 kAssetGeneric = IM_COL32(132, 132, 140, 255);

struct Fonts {
    ImFont* regular = nullptr;
    ImFont* semibold = nullptr;
    ImFont* mono = nullptr;
};

constexpr float kFontSize = 14.0f;
constexpr float kSmallFontSize = 12.0f;
constexpr float kMonoFontSize = 13.0f;

// Грузит Inter, Inter SemiBold (обе с иконками Lucide) и JetBrains Mono. Без файлов — шрифт ImGui по умолчанию.
void loadFonts();
const Fonts& fonts();
// Сбрасывает стиль и применяет тему в заданном масштабе интерфейса.
void apply(float uiScale = 1.0f);
float uiScale();

ImVec4 toVec4(ImU32 color);
ImU32 withAlpha(ImU32 color, float alpha);
ImU32 mix(ImU32 a, ImU32 b, float t);
} // namespace EditorTheme
