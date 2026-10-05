#include "editor/panels/RendererInfoPanel.h"

#include "editor/EditorContext.h"
#include "editor/EditorTheme.h"
#include "editor/EditorWidgets.h"
#include "editor/EditorWindows.h"
#include "editor/IconsLucide.h"
#include "render/IRenderAdapter.h"
#include "resources/ResourceManager.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>

using namespace EditorTheme;

namespace {
std::string formatBytes(std::size_t bytes) {
    char buffer[32] = {};
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024.0) {
        std::snprintf(buffer, sizeof(buffer), "%.0f KB", kb);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f MB", kb / 1024.0);
    }
    return buffer;
}

// Крупная цифра с подписью — плитка метрики.
void metricTile(const char* label, const char* value, ImU32 accent, float width) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float height = 54.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, ImVec2(min.x + width, min.y + height), ImGui::GetColorU32(kPanelRaised), 6.0f);
    drawList->AddRectFilled(min, ImVec2(min.x + 3.0f, min.y + height), ImGui::GetColorU32(accent), 6.0f, ImDrawFlags_RoundCornersLeft);
    ImGui::PushFont(fonts().semibold, 20.0f);
    drawList->AddText(ImVec2(min.x + 12.0f, min.y + 6.0f), ImGui::GetColorU32(kText), value);
    ImGui::PopFont();
    EditorUI::pushSmallFont();
    drawList->AddText(ImVec2(min.x + 12.0f, min.y + 33.0f), ImGui::GetColorU32(kTextDim), label);
    EditorUI::popFont();
    ImGui::Dummy(ImVec2(width, height));
}
}

void RendererInfoPanel::draw(EditorContext& context) {
    if (!open) {
        return;
    }
    if (!ImGui::Begin(EditorWindow::kRendererInfo, &open)) {
        ImGui::End();
        return;
    }
    drawFrame(context);
    drawScene(context);
    drawResources(context);
    drawAnimation(context);
    drawHeavyLoad(context);
    drawStress(context);
    ImGui::End();
}

void RendererInfoPanel::drawFrame(EditorContext& context) {
    if (!EditorUI::sectionHeader("frame", ICON_LC_GAUGE, "Frame")) {
        return;
    }
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float tileWidth = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;
    float minMs = FLT_MAX;
    float maxMs = 0.0f;
    float sum = 0.0f;
    int samples = 0;
    for (float ms : context.frameTimesMs) {
        if (ms <= 0.0f) {
            continue;
        }
        minMs = std::min(minMs, ms);
        maxMs = std::max(maxMs, ms);
        sum += ms;
        ++samples;
    }
    const float average = samples > 0 ? sum / samples : 0.0f;
    char fps[32];
    char frame[32];
    char worst[32];
    std::snprintf(fps, sizeof(fps), "%.0f", context.fpsAverage);
    std::snprintf(frame, sizeof(frame), "%.2f ms", average);
    std::snprintf(worst, sizeof(worst), "%.2f ms", maxMs);
    metricTile("Frames per second", fps, kSuccess, tileWidth);
    ImGui::SameLine();
    metricTile("Average frame", frame, kAccent, tileWidth);
    ImGui::SameLine();
    metricTile("Worst in 4 s", worst, maxMs > 33.0f ? kError : kWarning, tileWidth);

    // График времени кадра: последние кадры по порядку.
    float ordered[EditorContext::kFrameHistory];
    for (int i = 0; i < EditorContext::kFrameHistory; ++i) {
        ordered[i] = context.frameTimesMs[(context.frameHistoryOffset + i) % EditorContext::kFrameHistory];
    }
    const float scaleMax = std::max(20.0f, maxMs * 1.2f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, toVec4(IM_COL32(22, 22, 24, 255)));
    ImGui::PushStyleColor(ImGuiCol_PlotLines, toVec4(kAccentHovered));
    char overlay[48];
    std::snprintf(overlay, sizeof(overlay), "min %.1f  avg %.1f  max %.1f ms", minMs == FLT_MAX ? 0.0f : minMs, average, maxMs);
    ImGui::PlotLines("##frame_times", ordered, EditorContext::kFrameHistory, 0, overlay, 0.0f, scaleMax,
        ImVec2(ImGui::GetContentRegionAvail().x, 70.0f));
    ImGui::PopStyleColor(2);
    // Линия 16.7 мс — бюджет 60 FPS.
    const ImVec2 plotMin = ImGui::GetItemRectMin();
    const ImVec2 plotMax = ImGui::GetItemRectMax();
    const float budgetY = plotMax.y - (16.67f / scaleMax) * (plotMax.y - plotMin.y);
    ImGui::GetWindowDrawList()->AddLine(ImVec2(plotMin.x, budgetY), ImVec2(plotMax.x, budgetY), ImGui::GetColorU32(withAlpha(kSuccess, 0.45f)), 1.0f);
    EditorUI::componentSpacing();
}

void RendererInfoPanel::drawScene(EditorContext& context) {
    if (!EditorUI::sectionHeader("scene_stats", ICON_LC_BOXES, "Scene")) {
        return;
    }
    if (EditorUI::beginProperties("##scene_stats_table")) {
        const std::size_t entities = context.world.getEntityCount() - (context.world.isAlive(context.editorCameraEntity) ? 1u : 0u);
        EditorUI::propertyValue("Entities", std::to_string(entities).c_str());
        EditorUI::propertyValue("Meshes drawn", std::to_string(context.sceneDrawnMeshes).c_str());
        EditorUI::propertyValue("Collisions", std::to_string(context.physicsSystem.getLastCollisionCount()).c_str());
        EditorUI::propertyValue("Animated", std::to_string(context.animationSystem.characterCount).c_str());
        EditorUI::endProperties();
    }
    EditorUI::componentSpacing();
}

void RendererInfoPanel::drawResources(EditorContext& context) {
    if (!EditorUI::sectionHeader("resources", ICON_LC_DATABASE, "Resources")) {
        return;
    }
    ResourceManager& resources = ResourceManager::getInstance();
    if (EditorUI::beginProperties("##resources_table")) {
        EditorUI::propertyValue("Meshes", std::to_string(resources.getMeshCount()).c_str());
        EditorUI::propertyValue("Textures", std::to_string(resources.getTextureCount()).c_str());
        EditorUI::propertyValue("Shaders", std::to_string(resources.getShaderCount()).c_str());
        EditorUI::propertyValue("GPU textures alive", std::to_string(context.renderer.liveTextureCount()).c_str());
        EditorUI::propertyValue("Memory (estimate)", formatBytes(resources.estimateMemoryUsageBytes()).c_str());
        EditorUI::propertyLabel("Loads pending");
        ImGui::AlignTextToFramePadding();
        const std::size_t pending = resources.pendingLoadCount();
        if (pending > 0) {
            ImGui::TextColored(toVec4(kAccentHovered), ICON_LC_LOADER_CIRCLE "  %zu", pending);
        } else {
            ImGui::TextUnformatted("0");
        }
        EditorUI::endProperties();
    }
    EditorUI::componentSpacing();
}

void RendererInfoPanel::drawAnimation(EditorContext& context) {
    if (!EditorUI::sectionHeader("animation", ICON_LC_PERSON_STANDING, "Animation  \xC2\xB7  Lab 1")) {
        return;
    }
    AnimationSystem& animation = context.animationSystem;
    if (EditorUI::beginProperties("##animation_table")) {
        EditorUI::propertyCheckbox("Parallel poses", animation.parallel);
        EditorUI::propertyCheckbox("Pause all", animation.paused);
        EditorUI::propertyLabel("Global speed");
        ImGui::SliderFloat("##global_speed", &animation.speed, -2.0f, 3.0f, "%.2fx");
        EditorUI::propertyLabel("Characters per job");
        int batch = static_cast<int>(animation.batchSize);
        if (ImGui::SliderInt("##batch", &batch, 1, 256)) {
            animation.batchSize = static_cast<unsigned int>(batch);
        }
        char cpu[48];
        std::snprintf(cpu, sizeof(cpu), "%zu animated  \xC2\xB7  %.3f ms", animation.characterCount, animation.lastUpdateMs);
        EditorUI::propertyValue("CPU update", cpu);
        EditorUI::endProperties();
    }
    EditorUI::pushSmallFont();
    EditorUI::textFaint("CPU time includes prepare, dispatch and wait. Rendering is measured in Tracy.");
    EditorUI::popFont();

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::BeginDisabled(context.isPlaying() || context.animationLoad != nullptr);
    if (EditorUI::beginProperties("##crowd_table")) {
        EditorUI::propertyLabel("Model path", "A model with a skeleton and a baked clip. Relative to the executable or absolute.");
        ImGui::InputTextWithHint("##model_path", "assets/models/character/character.gltf", context.animationPath.data(), context.animationPath.size());
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("UZ_ASSET")) {
                std::snprintf(context.animationPath.data(), context.animationPath.size(), "%s", static_cast<const char*>(payload->Data));
            }
            ImGui::EndDragDropTarget();
        }
        EditorUI::propertyCheckbox("Model uses Y up", context.animationYUp);
        EditorUI::propertyLabel("Characters");
        ImGui::InputInt("##count", &context.animationDemoCount);
        context.animationDemoCount = std::clamp(context.animationDemoCount, 1, 2048);
        EditorUI::endProperties();
    }
    ImGui::BeginDisabled(context.animationPath[0] == '\0');
    if (EditorUI::primaryButton(ICON_LC_PERSON_STANDING "  Build Crowd")) {
        context.rebuildAnimationDemo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(context.animationDemoEntities.empty());
    if (ImGui::Button(ICON_LC_TRASH_2 "  Remove Crowd")) {
        context.removeAnimationDemo();
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (context.animationLoad) {
        EditorUI::iconLabel(ICON_LC_LOADER_CIRCLE, "Loading model in background...", kAccentHovered, kTextDim);
    } else if (!context.animationError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kWarning));
        ImGui::TextWrapped("%s", context.animationError.c_str());
        ImGui::PopStyleColor();
    }
    EditorUI::componentSpacing();
}

void RendererInfoPanel::drawHeavyLoad(EditorContext& context) {
    if (!EditorUI::sectionHeader("heavy_load", ICON_LC_HARD_DRIVE, "Heavy Load  \xC2\xB7  Lab 1", false)) {
        return;
    }
    LoadScenario& load = context.heavyLoad;
    ImGui::BeginDisabled(load.isRunning() || context.stress.isRunning());
    EditorUI::checkbox("Async (job system)", &context.heavyLoadAsync);
    ImGui::SetItemTooltip("On: decode on job workers, upload a few textures per frame.\nOff: the old synchronous path on the main thread.");
    if (ImGui::Button(ICON_LC_ZAP "  Burst")) {
        load.start(LoadScenario::Mode::Burst, context.heavyLoadAsync);
    }
    ImGui::SetItemTooltip("Request every image in assets/models in one frame.\nEach run reloads them from disk.");
    ImGui::SameLine();
    if (ImGui::Button(ICON_LC_WAYPOINTS "  Stream")) {
        load.start(LoadScenario::Mode::Stream, context.heavyLoadAsync);
    }
    ImGui::SetItemTooltip("Request one image every 100 ms, at most one per frame.\nEach run reloads them from disk.");
    ImGui::EndDisabled();
    if (load.totalCount() > 0) {
        const float progress = static_cast<float>(load.requestedCount()) / static_cast<float>(load.totalCount());
        char overlay[96];
        std::snprintf(overlay, sizeof(overlay), "%s %s  %zu/%zu  %.1f ms%s", LoadScenario::modeName(load.mode()),
            load.isAsync() ? "async" : "sync", load.requestedCount(), load.totalCount(), load.elapsedMs(), load.isRunning() ? "" : "  done");
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, toVec4(load.isRunning() ? kAccent : kSuccess));
        ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 0.0f), overlay);
        ImGui::PopStyleColor();
        if (load.fromCacheCount() > 0) {
            EditorUI::pushSmallFont();
            ImGui::TextDisabled("%zu taken from cache: still held elsewhere, e.g. by thumbnails", load.fromCacheCount());
            EditorUI::popFont();
        }
    }
    EditorUI::componentSpacing();
}

void RendererInfoPanel::drawStress(EditorContext& context) {
    if (!EditorUI::sectionHeader("stress", ICON_LC_FLAME, "Stress  \xC2\xB7  Lab 1", false)) {
        return;
    }
    StressRun& stress = context.stress;
    bool running = stress.isRunning();
    ImGui::BeginDisabled(context.heavyLoad.isRunning() && !running);
    if (EditorUI::checkbox("Load and unload in a loop", &running)) {
        if (running) {
            stress.start();
        } else {
            stress.stop();
        }
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Burst and stream in turn, through the job system.\nMemory and live GPU textures at cycle start must stay flat.");
    const StressStats& stats = stress.stats();
    if (stress.isRunning() || stats.cycles > 0) {
        if (EditorUI::beginProperties("##stress_table")) {
            EditorUI::propertyValue("Cycles", std::to_string(stats.cycles).c_str());
            EditorUI::propertyValue("Failed textures", std::to_string(stats.failedTextures).c_str());
            EditorUI::propertyValue("From cache", std::to_string(stats.fromCache).c_str());
            char hitches[48];
            std::snprintf(hitches, sizeof(hitches), "%zu  (worst %.1f ms)", stats.hitches, stats.worstFrameMs);
            EditorUI::propertyValue("Frames over 33 ms", hitches);
            EditorUI::propertyValue("Memory now", formatBytes(static_cast<std::size_t>(stats.footprintNow)).c_str());
            EditorUI::propertyValue("Memory at 1st cycle",
                formatBytes(stats.cycleStartFootprint.empty() ? 0 : static_cast<std::size_t>(stats.cycleStartFootprint.front())).c_str());
            EditorUI::propertyValue("GPU textures at cycle",
                std::to_string(stats.cycleStartTextures.empty() ? 0 : stats.cycleStartTextures.back()).c_str());
            EditorUI::endProperties();
        }
    }
    EditorUI::componentSpacing();
}
