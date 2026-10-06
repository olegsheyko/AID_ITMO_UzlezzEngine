#include "states/EditorState.h"
#include "prefabs/PrefabManager.h"
#include "input/InputManager.h"
#include <imgui.h>
#include <algorithm>
#include <type_traits>

void EditorState::loadArenaScene() {
    scripts_.stop();
    world_.clear();
    animationDemoEntities_.clear();
    animationLoad_.reset();
    selectedEntity_=controllableEntity_=gameCameraEntity_=editorCameraEntity_=prefabPreview_=kInvalidEntity;
    try {
        PrefabManager::spawn(world_,"assets/prefabs/arena.json");
        selectedEntity_=PrefabManager::spawn(world_,"assets/prefabs/core.json");
        PrefabManager::spawn(world_,"assets/prefabs/waves.json");
        prefabPreview_=PrefabManager::spawn(world_,"assets/prefabs/enemy.json");
        world_.getComponent<Tag>(prefabPreview_).name="Enemy prefab (Edit preview)";
        world_.getComponent<Transform>(prefabPreview_).position={-5,0,0};
        scripts_.reload();
        scriptUiMessage_="Loaded. Select an entity to edit script fields. Save prefab before Play.";
    } catch(const std::exception& e) {scriptUiMessage_=e.what();}
    scripts_.spawnPrefab=[this](const std::string& path){return PrefabManager::spawn(world_,path);};
    scripts_.inputPressed=[](const std::string& action){return InputManager::getInstance().isActionPressed(action);};
    createGameCamera();
    auto& camera=world_.getComponent<Transform>(gameCameraEntity_);
    camera.position={0,-24,16};camera.rotation={-0.58f,0,0};
    editorCamera_.focus({0,0,0},12);
    createEditorCameraEntity();setCameraMode();
}

void EditorState::renderScriptingPanel() {
    ImGui::Begin("Gameplay");
    ImGui::BeginDisabled(mode_==EditorMode::Play);
    if(ImGui::Button("Open Arena"))loadArenaScene();
    ImGui::SameLine();
    if(ImGui::Button("Reload scripts"))scriptUiMessage_=scripts_.reload()?"Scripts reloaded successfully.":"Reload failed; previous classes retained.";
    ImGui::EndDisabled();
    ImGui::TextWrapped("Play: focus viewport. Space: start next wave. F: defense pulse. Stop restores the edited scene.");
    ImGui::TextWrapped("%s",scriptUiMessage_.c_str());
    if(mode_==EditorMode::Play)ImGui::TextWrapped("%s",scripts_.status().c_str());
    if(!scripts_.error().empty()) {
        ImGui::Separator();ImGui::TextUnformatted("Lua error:");
        ImGui::TextWrapped("%s",scripts_.error().c_str());
    }
    ImGui::End();
}

void EditorState::renderScriptInspector() {
    if(!world_.hasComponent<ScriptComponent>(selectedEntity_))return;
    auto& c=world_.getComponent<ScriptComponent>(selectedEntity_);
    if(!ImGui::TreeNodeEx("Script",ImGuiTreeNodeFlags_DefaultOpen))return;
    ImGui::TextWrapped("%s :: %s",c.path.c_str(),c.className.c_str());
    for(auto& [name,value]:c.fields)std::visit([&](auto& v){
        using T=std::decay_t<decltype(v)>;
        if constexpr(std::is_same_v<T,bool>)ImGui::Checkbox(name.c_str(),&v);
        else if constexpr(std::is_same_v<T,int>)ImGui::InputInt(name.c_str(),&v);
        else if constexpr(std::is_same_v<T,double>)ImGui::InputDouble(name.c_str(),&v);
        else {char buffer[1024]{};std::copy_n(v.data(),std::min(v.size(),sizeof(buffer)-1),buffer);if(ImGui::InputText(name.c_str(),buffer,sizeof(buffer)))v=buffer;}
    },value);
    if(ImGui::Button("Save fields to prefab")) {
        std::string error;
        scriptUiMessage_=PrefabManager::saveFields(c,error)?"Prefab fields saved; used by next spawn / scene load.":error;
    }
    ImGui::TreePop();
}
