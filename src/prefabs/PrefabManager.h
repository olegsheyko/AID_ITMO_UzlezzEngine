#pragma once
#include "ecs/World.h"
#include "scripting/ScriptComponent.h"
#include <string>

class PrefabManager {
public:
    // renderResources=false supports headless gameplay tests using the same data.
    static Entity spawn(World& world, const std::string& path, bool renderResources = true);
    static bool saveFields(const ScriptComponent& component, std::string& error);
};
