---@meta uzlezz
-- API движка для lua-language-server (VS Code: расширение sumneko.lua, конфиг — .luarc.json в корне).
-- Только аннотации, движок этот файл не загружает. Биндинги — src/scripting/ScriptSystem.cpp;
-- ScriptRuntimeTests проверяет, что здесь описано всё, что там привязано.

---Пространство имён движка.
---@class uzlezz
uzlezz = {}

---@alias uzlezz.FieldValue boolean|integer|number|string

---Вектор передаётся копией: изменить позицию — значит вызвать set_position с новым Vec3.
---@class uzlezz.Vec3
---@field x number
---@field y number
---@field z number
---@overload fun(x: number, y: number, z: number): uzlezz.Vec3
local Vec3 = {}
uzlezz.Vec3 = Vec3

---Хэндл сущности: id + поколение + время жизни мира, а не указатель на компонент.
---После destroy или Stop is_alive() возвращает false, остальные методы бросают ошибку.
---@class uzlezz.Entity
---@field id integer
local Entity = {}
uzlezz.Entity = Entity

---@return boolean
function Entity:is_alive() end

---@return uzlezz.Vec3
function Entity:get_position() end

---@param position uzlezz.Vec3
function Entity:set_position(position) end

---Поворот вокруг вертикальной оси Z, радианы.
---@param yaw number
function Entity:set_yaw(yaw) end

---@return string
function Entity:get_tag() end

---Скорость анимации Animator; без Animator ничего не делает.
---@param speed number
function Entity:set_animation_speed(speed) end

---Поле скриптового поведения: объявлено в fields класса, значение — из префаба или инспектора.
---@param name string
---@return uzlezz.FieldValue
function Entity:get_field(name) end

---Тип сохраняется: целое поле принимает только целое, number принимает и целое.
---@param name string
---@param value uzlezz.FieldValue
function Entity:set_field(name, value) end

---Сервисы мира для скрипта. Действителен только в Play.
---@class uzlezz.World
local World = {}
uzlezz.World = World

---Действие нажато в этом кадре (StartWave, DefensePulse…). Только когда у окна Game фокус.
---@param action string
---@return boolean
function World:input_pressed(action) end

---Первая живая сущность с тегом; если нет — хэндл, у которого is_alive() == false.
---@param tag string
---@return uzlezz.Entity
function World:find_by_tag(tag) end

---Все живые сущности с тегом, массив с 1.
---@param tag string
---@return uzlezz.Entity[]
function World:find_all_by_tag(tag) end

---Создать экземпляр префаба (JSON из assets/prefabs) одной командой.
---@param path string
---@param position uzlezz.Vec3
---@return uzlezz.Entity
function World:spawn_prefab(path, position) end

---Уничтожить сущность после текущего update.
---@param entity uzlezz.Entity
function World:destroy(entity) end

---Строка статуса для HUD и окна Gameplay.
---@param text string
function World:set_status(text) end

---C++-база поведения. Свой класс объявляется через uzlezz.behaviour и аннотацию
---`---@class MyBehaviour : uzlezz.Behaviour`. Необъявленный callback ничего не делает.
---@class uzlezz.Behaviour
---@field entity uzlezz.Entity сущность, к которой прикреплён скрипт
---@field world uzlezz.World сервисы мира
---@field fields table<string, uzlezz.FieldValue> объявление полей: имя → значение по умолчанию (тип берётся из него)
local ScriptBehaviour = {}
uzlezz.ScriptBehaviour = ScriptBehaviour

---Вызывается при Play или при спавне сущности.
function ScriptBehaviour:on_create() end

---Каждый кадр Play.
---@param dt number секунды
function ScriptBehaviour:on_update(dt) end

---При уничтожении сущности и при Stop. World в Stop уже недоступен.
function ScriptBehaviour:on_destroy() end

---Объявить класс поведения, расширяющий C++-базу.
---@generic T
---@param prototype T таблица с fields и callbacks
---@return T
function uzlezz.behaviour(prototype) end

---Печать в консоль редактора и engine.log с файлом и строкой вызова.
---@param ... any
function print(...) end
