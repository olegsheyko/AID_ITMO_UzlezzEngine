#pragma once

#include "ecs/Components.h"
#include <imgui.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class TexturePicker {
public:
    void render(MeshRenderer& meshRenderer);

private:
    ImGuiTextFilter filter_;
    std::vector<std::string> paths_;
    // Null entries remember decode failures until the popup is reopened.
    std::unordered_map<std::string, std::shared_ptr<Resource<TextureData>>> previews_;
};
