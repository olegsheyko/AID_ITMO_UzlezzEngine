#pragma once
#include "ecs/World.h"
#include "scripting/ScriptComponent.h"
#include <string>

// Префаб это JSON-шаблон сущности (assets/prefabs/*.json): набор компонентов и их начальные значения.
// PrefabManager умеет создать сущность из такого файла и записать изменённые поля скрипта обратно.
// Через него появляются и объекты сцены Arena, и враги, которых спавнит waves.lua.
class PrefabManager {
public:
    // Создаёт сущность в мире по JSON-файлу. Бросает исключение при ошибке, при этом
    // уже созданная часть сущности удаляется, мир не остаётся с «полусобранным» объектом.
    // renderResources=false supports headless gameplay tests using the same data.
    static Entity spawn(World& world, const std::string& path, bool renderResources = true);
    // Записывает текущие поля скрипта (из Inspector) в исходный JSON-префаб. false и текст в error при неудаче.
    static bool saveFields(const ScriptComponent& component, std::string& error);
};
