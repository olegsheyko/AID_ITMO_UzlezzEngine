# ЛР 2 — Lua, префабы и волны противников

**Lua 5.4.8 + sol2 3.3.1**, C++17. Lua компилируется из исходников статически
в exe: Python, отдельная Lua DLL или установленный интерпретатор не нужны.
Сценарий показа — [DEMO.md](DEMO.md).

## Сборка и запуск

### macOS

Нужны Homebrew-пакеты `cmake glfw assimp`. Из корня репозитория:

```bash
tools/setup_lua.sh
cmake -S . -B build/mac -DCMAKE_BUILD_TYPE=Release
cmake --build build/mac --parallel
ctest --test-dir build/mac --output-on-failure
./build/mac/GameEngine --editor-arena
```

В VS Code: Cmd+Shift+B — сборка, задача «Engine: run» — запуск, «Engine + Tracy» — с профайлером.

### Windows x64 (Visual Studio 2022)

```powershell
powershell -ExecutionPolicy Bypass -File tools/setup_lua.ps1
cmake -S . -B build/lab2-lua -G "Visual Studio 17 2022" -A x64 -DENGINE_BUILD_TRACY_DEMOS=OFF
cmake --build build/lab2-lua --config Release --parallel 6
ctest --test-dir build/lab2-lua -C Release --output-on-failure
./build/lab2-lua/Release/GameEngine.exe --editor-arena
```

### Откуда запускать

**Из корня репозитория.** Пути к контенту относительны рабочей папки. Только так
правка `assets/scripts/*.lua` сразу видна движку, а Save to Prefab пишет в
исходный `assets/prefabs/*.json`. Если запускать из `build/...`, движок работает
с копией `assets`, и следующая сборка (`CopyAssets`) её затирает.

### Что нужно на машине пользователя

Setup-скрипты загружают закреплённые исходники [Lua](https://www.lua.org/ftp/) и
[sol2](https://github.com/ThePhD/sol2/tree/v3.3.1) и проверяют SHA-256. Каталоги
игнорируются Git. Лицензии MIT сборка копирует в `licenses/` рядом с exe.

Пользователю нужны exe, `assets` и `licenses`. Lua внутри exe, ничего не ищется в системе.
Платформенные зависимости те же, что и до ЛР 2:
- Windows: DLL Assimp и Visual C++ Runtime;
- macOS: dylib GLFW и Assimp из Homebrew.

## Демонстрация

1. Запуск с `--editor-arena` или кнопка **Open Arena** в окне **Gameplay**
   (File → Open Arena (Lua) тоже работает).
2. Выделите Core, Waves или «Enemy prefab (Edit preview)». В карточке Script
   инспектора поменяйте поля и нажмите **Save to Prefab**. Порядок ключей JSON сохраняется.
3. Play (Ctrl/⌘+P), затем щёлкните окно Game, чтобы оно получило ввод.
4. Space запускает волну, следующая доступна после уничтожения предыдущей.
   F — импульс ядра (cooldown 2 с).
5. HUD в окне Game и окно Gameplay показывают HP, номер волны, живых и ожидающих врагов,
   кучу Lua и время скриптов за кадр.
6. Правка `.lua` прямо в Play: сохраните файл, и через долю секунды игра идёт на новом
   коде без Stop. Режим «In Play» в окне Gameplay:
   - **Keep state (L2)** переносит состояние;
   - **Reset state (L1)** запускает экземпляры заново.

   Синтаксическая ошибка не останавливает игру: прежний код работает дальше,
   а в Console и Gameplay видно `файл:строка`.

## Архитектура

- **Один `sol::state` на редактор.** Его создаёт `ScriptSystem::Impl`: открывает
  base/math/table/string без `dofile`/`loadfile`, переводит `print` в лог движка,
  регистрирует `uzlezz`. Перед state объявлен `Sandbox` (аллокатор и бюджет), после него —
  ссылки sol2: state уничтожается после ссылок, `Sandbox` — после state.
  Stop освобождает экземпляры, VM живёт до закрытия редактора.
- **C++-база и адаптер.** `ScriptBehaviour` — C++-база с виртуальными
  `on_create`/`on_update`/`on_destroy`. `LuaBehaviour` — C++-адаптер (trampoline),
  владеющий таблицей экземпляра. Каждый вызов — `protected_function` с traceback,
  бюджетом времени и зоной Tracy.
- **Классы поведения.** `uzlezz.behaviour {...}` создаёт прототип, расширяющий C++-базу
  через метатаблицу. Экземпляр — своя таблица с `entity` и `world`.
- **Данные компонента.** `ScriptComponent` содержит только сериализуемые данные (путь,
  класс, префаб, поля), без Lua-ссылок.
- **Префабы.** `PrefabManager` создаёт сущность из JSON, откатывает неудачный spawn и
  сохраняет поля.
- **Механика.** `core.lua` — HP и импульс, `enemy.lua` — движение, урон, анимация,
  `waves.lua` — волны, ввод, статус, поражение. Решений механики в C++ нет.

```lua
---@class Mover : uzlezz.Behaviour
Mover = uzlezz.behaviour { fields = { speed = 1.0 } }
function Mover:on_update(dt)
    local p = self.entity:get_position()
    p.x = p.x + self.entity:get_field("speed") * dt
    self.entity:set_position(p)
end
```

**API:**
- `Vec3`;
- `Entity`: `is_alive`, `id`, позиция, yaw, тег, скорость анимации, `get_field`/`set_field`;
- `World`: `input_pressed`, `find_by_tag`, `find_all_by_tag`, `spawn_prefab`, `destroy`, `set_status`.

`find_all_by_tag` возвращает массив с 1 для `ipairs` и `#`.
Полное описание с типами — [`tools/lua-stubs/uzlezz.lua`](../../tools/lua-stubs/uzlezz.lua).

**Поля поведения.** `fields` задаёт типы и значения по умолчанию, JSON — переопределения.
Поддержаны boolean, integer, number и string. Целое приводится к number
(`"speed": 2` для `speed = 1.0` работает), number к целому — только без дробной части.
Поле, удалённое из Lua, выбрасывается из компонента с предупреждением. Поле со сменившимся
типом сбрасывается к значению по умолчанию. Play snapshot сохраняет сцену до Play, Stop её
восстанавливает. Runtime-значения не записываются.

## Допфичи

### Hot reload через job system и в Play (L1/L2)

`ScriptWatcher` раз в 200 мс ставит фоновую задачу job system ЛР 1 (`submitBackground`).
Задача делает пять вещей:
- обходит `assets/scripts` и файлы сцены;
- сравнивает метку `last_write_time` + размер;
- ждёт, пока метка не изменится два опроса подряд;
- читает файл;
- компилирует его в одноразовом `lua_State`.

Применение — `ScriptSystem::hotReload` на главном потоке, транзакционно: при ошибке
работает прежний код.

В Play каждый экземпляр получает новый объект и свой `on_create`, затем по режиму:
- **L2** переносит `self.*` с тем же типом. Исключение — поле, чьё начальное значение
  в коде поменяли: правка `self.cooldown = 5.0` в `on_create` видна сразу.
- **Своя миграция.** Если класс объявил `on_reload(old)`, перенос делает он.
- **L1** оставляет состояние из `on_create`.

Экземпляры, отключённые runtime-ошибкой, оживают с исправленным кодом.
Кнопка Reload Scripts в Play делает то же для всех файлов сцены.

### Профилирование в Tracy

- Зоны `Lua: update`, `Lua: reload`, `Lua: hot reload`, `Lua: load file`.
- Зона на каждый entry point с именем `Enemy:on_update`, `Waves:on_update`…
- Зоны вотчера `Lua watcher: scan/validate` на потоке воркера.
- Графики `Lua memory KB` и `Lua instances`.

### Sandbox: лимиты скриптов

- **Время.** Count-хук раз в 1000 инструкций проверяет бюджет вызова (100 мс). Бесконечный
  цикл превращается в ошибку `файл:строка: script exceeded the 100 ms time budget`.
  Отключается только этот экземпляр, движок и остальные скрипты работают.
- **Память.** Собственный аллокатор `lua_newstate` с потолком кучи (256 МБ). Скрипт,
  который выделяет больше, получает ошибку `Lua heap limit exceeded`, а не съедает ОЗУ.

### Подсказки в IDE

`tools/lua-stubs/uzlezz.lua` — аннотации lua-language-server: классы, методы, типы
параметров. `.luarc.json` подключает их, `.vscode/extensions.json` рекомендует
расширение `sumneko.lua`. Скрипты помечены `---@class Enemy : uzlezz.Behaviour`,
так что после `self.entity:` работает автодополнение. Тест сверяет stubs с реальными
метатаблицами usertype и ловит и пропущенный, и несуществующий метод.

## Владение и время жизни

- **Entity — handle, а не указатель на компонент.** Проверяются weak lifetime мира,
  поколение ID и существование. Vec3 передаётся копией.
- **World proxy** инвалидируется при Stop и не активируется повторно.
- **Поток и уничтожение.** Все вызовы Lua и ECS выполняются на главном потоке.
  Destroy применяется после update.
- **Ошибки.** Ошибки C++-биндингов несут место вызова в Lua: sol2-обработчик добавляет
  `luaL_where`. Runtime-ошибка отключает экземпляр до следующего Play или hot reload.
- **Сборка мусора.** Lua GC выполняется при Stop/Reload и инкрементально во время Play.
  Куча видна в окне Gameplay и на графике Tracy.
- **Ограничения.** `package/require`, `io/os/debug` не открыты, но это не полноценная
  изоляция. Запись в `_G` переживает reload, поэтому игровое состояние в `_G` не хранить.

## Проверки

- **`ScriptSystemTests`:**
  - реальная Lua VM и игровые скрипты на закреплённых копиях префабов;
  - spawn, движение, урон, импульс;
  - 30 минут симулированного времени без роста кучи;
  - 100 циклов Stop/Reload/Play;
  - handles, ошибки, reload, сохранение JSON.
- **`ScriptRuntimeTests`:**
  - файл:строка у ошибок биндингов;
  - `print` в лог;
  - приведение типов полей;
  - бесконечный цикл и лимит памяти;
  - stubs совпадают с биндингами;
  - hot reload в Play (L2, L1, `on_reload`, сломанный файл, оживление, 200 перезагрузок подряд);
  - вотчер на job system.

Сценарий живой проверки и заготовленные правки правил — в [DEMO.md](DEMO.md).
