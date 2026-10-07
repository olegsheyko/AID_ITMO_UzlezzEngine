# ЛР 2 — сценарий демо (3 минуты)

Запуск из корня репозитория, чтобы правки `assets/` были живыми:

```bash
./build/mac/GameEngine --editor-arena
```

Рядом открыть VS Code с `assets/scripts/`. В окне **Gameplay**: «Reload on save» включён,
режим «In Play: Keep state (L2)».

## Ход показа

| Время | Действие | Что видно |
|---|---|---|
| 0:00 | Арена открыта. Выделить **Core** → карточка Script | Поля `health`, `pulse_radius`, `pulse_cooldown` из `core.json` |
| 0:15 | Выделить «Enemy prefab (Edit preview)», `damage` 1 → 2, **Save to Prefab** | `assets/prefabs/enemy.json` изменился (`git diff`) |
| 0:30 | Play (⌘P), клик по Game, Space | Волна из 4 врагов идёт к ядру, HUD: HP / Wave / Alive |
| 0:50 | F, когда враги близко | Импульс уничтожает врагов в радиусе |
| 1:00 | Правка `enemy.lua` прямо в Play (правка А), ⌘S | Враги на поле сразу быстрее, номер волны и HP не сбросились |
| 1:30 | Сломать синтаксис (правка Д), ⌘S | Console: `assets/scripts/enemy.lua:N: …`, игра идёт на старом коде |
| 1:45 | Исправить, ⌘S | «enemy.lua reloaded in Play…» |
| 2:00 | Правило по просьбе преподавателя (правки Б–Г) | Изменение видно в следующей волне без перезапуска |
| 2:40 | Gameplay: Lua heap, Script time; Tracy — зоны `Enemy:on_update` | Куча не растёт, тик скриптов — доли мс |

## Заготовленные правки

### А. Число: скорость врагов (живьём в Play)

`assets/scripts/enemy.lua`, `on_update`:

```lua
local step = math.min(distance, self.entity:get_field("speed") * dt)
-- →
local step = math.min(distance, self.entity:get_field("speed") * 3 * dt)
```

### Б. Число и порядок: каждая следующая волна быстрее

`assets/scripts/waves.lua`, на месте `self.world:spawn_prefab(...)`:

```lua
local enemy = self.world:spawn_prefab(self.entity:get_field("enemy_prefab"),
    uzlezz.Vec3(center.x + math.cos(angle) * radius, center.y + math.sin(angle) * radius, 0.0))
enemy:set_field("speed", 1.5 + 0.5 * (self.wave - 1))
```

### В. Условие: импульс срабатывает, только если в радиусе не меньше трёх врагов

`assets/scripts/core.lua`, тело `if self.world:input_pressed("DefensePulse") ...`:

```lua
local center = self.entity:get_position()
local radius = math.max(0.0, self.entity:get_field("pulse_radius"))
local hit = {}
for _, enemy in ipairs(self.world:find_all_by_tag("Enemy")) do
    local p = enemy:get_position()
    local dx, dy = p.x - center.x, p.y - center.y
    if dx * dx + dy * dy <= radius * radius then hit[#hit + 1] = enemy end
end
if #hit >= 3 then
    for _, enemy in ipairs(hit) do self.world:destroy(enemy) end
    self.cooldown = math.max(0.0, self.entity:get_field("pulse_cooldown"))
end
```

### Г. Порядок: враги заходят против часовой стрелки или все с одной стороны

`assets/scripts/waves.lua`:

```lua
local angle = -math.pi / 2 + self.serial * (2 * math.pi / self.wave_count)
-- против часовой:
local angle = -math.pi / 2 - self.serial * (2 * math.pi / self.wave_count)
-- все с одной стороны (сектор 60°):
local angle = -math.pi / 2 + (self.serial / self.wave_count - 0.5) * math.pi / 3
```

### Д. Устойчивость

- **Синтаксическая ошибка:** удалить `end` в конце `on_update` и сохранить. В логе будет
  файл:строка, игра продолжится на прежнем коде.
- **Ошибка в рантайме:** `self.entity:get_field("helth")`. Экземпляр отключается с
  `enemy.lua:N: Enemy: unknown field 'helth'`, после исправления оживает.
- **Зависание:** `while true do end` в `Core:on_update`. Через 100 мс приходит ошибка
  `core.lua:N: script exceeded the 100 ms time budget`, редактор не завис. После
  исправления ядро оживает.

### Е. Сброс состояния (L1)

В Gameplay переключить «In Play» на **Reset state (L1)** и сохранить `waves.lua`.
Номер волны начинается заново, сцена и враги остаются.

## Ловушки

- **`damage` и `health` — целые.** Дробный урон (`1.5`) даёт ошибку
  `Core: field 'health' expects integer`, поэтому брать целые числа.
- **Поля number** принимают и целое (`"speed": 2`), это нормально.
- **Правки в Play** идут в код (`.lua`). Числа баланса в префабе (Save to Prefab)
  применяются к следующему спавну или следующему Play.
- **После демо** вернуть файлы: `git checkout assets/`.

## Шпаргалка ответов

- **Кто создаёт и убивает state.** `ScriptSystem::Impl` внутри `EditorContext`: создаётся
  с редактором, умирает с ним. Stop уничтожает экземпляры, VM остаётся. Перед state —
  `Sandbox` (аллокатор), после — ссылки sol2. Порядок разрушения обратный.
- **Что в state.** Библиотеки base/math/table/string, модуль `uzlezz`, классы поведения
  (по environment на файл), таблицы экземпляров. Игровое состояние в `_G` не хранится.
- **Почему нет висячих ссылок.** Lua держит handle {мир, weak lifetime, id, поколение},
  а не указатель. C++ держит только `sol::table`/`sol::reference`, которые умирают раньше state.
- **Дистрибуция.** Lua собирается из исходников в exe (`EngineLua`), в системе ничего не ищется.
- **Где job system.** Вотчер: обход, метки, чтение и компиляция на воркере
  (`submitBackground`). Применение только на главном потоке: VM однопоточная.
- **L2.** Новый объект и `on_create`, затем перенос `self.*` того же типа, кроме полей,
  чьё начальное значение в коде изменилось. `on_reload(old)` — ручная миграция.
