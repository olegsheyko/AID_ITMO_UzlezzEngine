#include "states/EditorState.h"
#include "resources/ResourceManager.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <limits>

void EditorState::renderAnimationPanel() {
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Animation (lab 1)", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::Checkbox("Parallel poses (job system)", &animationSystem_.parallel);
    ImGui::Checkbox("Pause all animations", &animationSystem_.paused);
    ImGui::SliderFloat("Global animation speed", &animationSystem_.speed, -2, 3);
    int batch = static_cast<int>(animationSystem_.batchSize);
    if (ImGui::SliderInt("Characters per job", &batch, 1, 256)) animationSystem_.batchSize = static_cast<unsigned int>(batch);
    ImGui::Text("Animated: %zu | CPU update: %.3f ms", animationSystem_.characterCount, animationSystem_.lastUpdateMs);
    ImGui::TextDisabled("CPU includes prepare, dispatch and wait. Rendering is measured separately in Tracy.");
    ImGui::BeginDisabled(mode_ == EditorMode::Play || animationLoad_ != nullptr);
    ImGui::InputTextWithHint("Model path", "assets/models/character/character.gltf", animationPath_.data(), animationPath_.size());
    ImGui::SetItemTooltip("A model with a skeleton and a baked animation clip. Paths are relative to the executable or absolute.\n"
        "Walking.fbx is the included demo. External textures are optional.");
    ImGui::Checkbox("Model uses Y up", &animationYUp_);
    ImGui::InputInt("Character count", &animationDemoCount_);
    animationDemoCount_ = std::clamp(animationDemoCount_, 1, 2048);
    ImGui::BeginDisabled(animationPath_[0] == '\0');
    if (ImGui::Button("Load / rebuild crowd")) rebuildAnimationDemo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Remove crowd")) {
        for (Entity entity : animationDemoEntities_) if (world_.isAlive(entity)) world_.destroyEntity(entity);
        animationDemoEntities_.clear();
    }
    ImGui::EndDisabled();
    if (!animationError_.empty()) ImGui::TextWrapped("%s", animationError_.c_str());
    ImGui::TextDisabled("Select a character in Hierarchy for clip, time and individual playback controls.");
}

void EditorState::rebuildAnimationDemo() {
    auto& resources = ResourceManager::getInstance();
    auto mesh = animationLoad_ ? animationLoad_ : resources.loadMeshAsync(animationPath_.data());
    if (mesh && mesh->isPending()) {
        animationLoad_ = mesh;
        animationError_ = "Loading model in background...";
        return;
    }
    animationLoad_.reset();
    if (!mesh || !mesh->isLoaded()) {
        animationError_ = "Import failed. Check the model path and engine log."; return;
    }
    const auto& data = *mesh->getData();
    const bool hasSkin = std::any_of(data.subMeshes.begin(), data.subMeshes.end(), [](const SubMesh& sub) { return !sub.bones.empty(); });
    if (!hasSkin || data.skeleton.clips.empty()) {
        animationError_ = "The model must contain skin weights, a skeleton and at least one animation clip."; return;
    }
    auto shader = resources.loadShader("assets/shaders/mesh_vertex.glsl", "assets/shaders/mesh_fragment_simple_texture.glsl");
    if (!shader) { animationError_ = "Skinning shader failed to compile. Check the engine log."; return; }
    // Fit the bind-pose geometry, not arbitrary source units, into a two-unit-tall character.
    Vec3 minimum{std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
    Vec3 maximum{-minimum.x,-minimum.y,-minimum.z};
    AnimationPose bind;
    Animation::preparePose(data, bind);
    Animation::evaluate(data, std::numeric_limits<unsigned int>::max(), 0, bind);
    for (size_t s=0; s<data.subMeshes.size(); ++s) {
        const auto& sub = data.subMeshes[s];
        for (const auto& vertex : sub.vertices) {
            Mat4 transform = Math::multiply(data.skeleton.rootInverse, bind.globals[sub.skeletonNode]);
            float total=0; for (float w : vertex.boneWeights) total+=w;
            if (!sub.bones.empty() && total > 0) {
                transform = {};
                for (size_t b=0; b<4; ++b) for (size_t k=0; k<16; ++k)
                    transform.values[k] += bind.palettes[s][vertex.boneIds[b]].values[k] * vertex.boneWeights[b];
            }
            const auto& m=transform.values; const auto& v=vertex.position;
            Vec3 p{m[0]*v.x+m[4]*v.y+m[8]*v.z+m[12],m[1]*v.x+m[5]*v.y+m[9]*v.z+m[13],m[2]*v.x+m[6]*v.y+m[10]*v.z+m[14]};
            if (animationYUp_) p={p.x,-p.z,p.y};
            minimum={std::min(minimum.x,p.x),std::min(minimum.y,p.y),std::min(minimum.z,p.z)};
            maximum={std::max(maximum.x,p.x),std::max(maximum.y,p.y),std::max(maximum.z,p.z)};
        }
    }
    const float scale = 2.0f / std::max(0.001f, maximum.z-minimum.z);
    const float spacing = std::max(2.5f, (maximum.x-minimum.x)*scale*1.4f);
    for (Entity entity : animationDemoEntities_) if (world_.isAlive(entity)) world_.destroyEntity(entity);
    animationDemoEntities_.clear();
    const int columns = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(animationDemoCount_))));
    for (int i=0; i<animationDemoCount_; ++i) {
        const Entity entity = world_.createEntity();
        animationDemoEntities_.push_back(entity);
        world_.addComponent<Tag>(entity, Tag{"Animated character " + std::to_string(i+1)});
        Transform transform;
        transform.position = {20+(i%columns-(columns-1)*0.5f)*spacing-(minimum.x+maximum.x)*0.5f*scale,
            (i/columns-(columns-1)*0.5f)*spacing-(minimum.y+maximum.y)*0.5f*scale,-minimum.z*scale};
        transform.scale={scale,scale,scale};
        if (animationYUp_) transform.rotation.x=1.57079632679f;
        world_.addComponent<Transform>(entity, transform);
        auto& renderer = world_.addComponent<MeshRenderer>(entity);
        renderer.meshId=animationPath_.data(); renderer.cachedMesh=mesh; renderer.cachedShader=shader;
        renderer.shaderId="assets/shaders/mesh_vertex.glsl|assets/shaders/mesh_fragment_simple_texture.glsl";
        auto& animator=world_.addComponent<Animator>(entity);
        animator.time=data.skeleton.clips.front().duration*static_cast<double>(i)/animationDemoCount_;
    }
    animationSystem_.update(world_, 0);
    selectedEntity_=animationDemoEntities_.front();
    editorCamera_.focus({20,0,1},std::max(3.0f,columns*spacing*0.65f));
    animationError_.clear();
}
