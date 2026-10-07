#pragma once
#include "ecs/Component.h"
#include <map>
#include <string>
#include <variant>

using ScriptValue = std::variant<bool, int, double, std::string>;
struct ScriptComponent {
    std::string path;
    std::string className;
    std::string prefab;
    std::map<std::string, ScriptValue> fields;
};
template<> struct IsComponent<ScriptComponent> : std::true_type {};
