#pragma once
#include "ecs/Component.h"
#include <map>
#include <string>
#include <variant>

// Допустимые типы значений полей скрипта. Только простые данные: их можно скопировать для снимка Play,
// показать в Inspector и записать в JSON префаба. Таблицы и функции Lua сюда не попадают.
using ScriptValue = std::variant<bool, int, double, std::string>;

// ECS-компонент «на этой сущности работает скрипт». Хранит только данные, без ссылок на Lua:
// поэтому компонент безопасно копируется (снимок Play, восстановление при Stop), а сама виртуальная
// машина и экземпляры живут отдельно в ScriptSystem.
struct ScriptComponent {
    std::string path;      // файл скрипта, например "assets/scripts/enemy.lua"
    std::string className; // имя Lua-класса внутри файла, например "Enemy"
    std::string prefab;    // из какого префаба создана сущность (нужно кнопке Save to Prefab), может быть пустым
    std::map<std::string, ScriptValue> fields; // поля баланса: значения из префаба или Inspector
};
template<> struct IsComponent<ScriptComponent> : std::true_type {};
