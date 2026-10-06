# ЛР 2 — Lua, префабы и волны противников

Базовая реализация без дополнительных фич. По варианту из задания:
**Lua 5.4.8 + sol2 3.3.1**, C++17. Lua компилируется из исходников статически
в exe; Python, pip, отдельная Lua DLL или установленный интерпретатор не нужны.

## Сборка и запуск

Из корня проекта (Windows x64, Visual Studio 2022, CMake):

```powershell
powershell -ExecutionPolicy Bypass -File tools/setup_lua.ps1
cmake -S . -B build/lab2-lua -G "Visual Studio 17 2022" -A x64 -DENGINE_BUILD_TRACY_DEMOS=OFF
cmake --build build/lab2-lua --config Release --parallel 6
ctest --test-dir build/lab2-lua -C Release --output-on-failure
./build/lab2-lua/Release/GameEngine.exe
```

Setup загружает закреплённые исходники [Lua](https://www.lua.org/ftp/) и
[sol2](https://github.com/ThePhD/sol2/tree/v3.3.1), проверяет SHA-256.
Зависимости игнорируются Git и восстанавливаются setup-скриптом на новой машине.
Лицензии MIT находятся в `external/lua/lua-5.4.8/doc/readme.html` и
`external/sol2/sol2-3.3.1/LICENSE.txt`. Сборка копирует их в `Release/licenses`;
сохраняйте этот каталог при распространении.

Для пользователя нужны exe, DLL-зависимости Assimp, `assets` и `licenses`,
а также Visual C++ Runtime, как и раньше. Lua внутри exe.
Можно запускать из корня исходников или Release: пути контента относительны
рабочей папке. Редактируйте именно используемый набор assets. Повторная сборка
CopyAssets заменяет копию в Release исходниками.

Старые локальные каталоги Python и предыдущая сборка `build/lab2` не удаляются,
но больше не подключаются. Чистая `build/lab2-lua` исключает их влияние.

## Демонстрация

1. В окне **Lab 2 - Lua** нажмите **Load Lab 2 scene** (заменяет текущую сцену).
2. Выберите Core, Waves или Enemy prefab (Edit preview) в Hierarchy.
   Inspector → Script: измените поля, нажмите **Save fields to prefab**.
   Для preview врага Save обязателен: новые враги создаются из JSON, не из preview.
3. Нажмите Play и щёлкните viewport для включения игрового ввода.
4. Space запускает волну; следующая доступна после уничтожения предыдущей.
   F уничтожает врагов в радиусе импульса (cooldown 2 секунды).
   Враги идут к ядру с анимацией Walking, наносят урон и исчезают.
5. Окно Lab 2 показывает HP, волну, живых/ожидающих врагов и поражение.
   Stop → Play возвращает исходные значения сцены и перезапускает механику.
6. В Edit измените `.lua`, нажмите **Reload scripts**, затем Play.
   Например, измените формулу урона в `enemy.lua`: правило меняется без сборки.
   Синтаксическая ошибка не закрывает редактор; UI и лог показывают файл:строку.

## Архитектура

- `ScriptSystem::Impl` создаёт один `sol::state` вместе с редактором,
  открывает base/math/table/string, регистрирует `uzlezz`. State объявлен перед
  ссылками sol2 и уничтожается после них. Stop освобождает экземпляры, но не VM;
  деструктор ScriptSystem закрывает VM при уничтожении состояния редактора.
- `ScriptBehaviour` — C++-база с виртуальными on_create/on_update/on_destroy.
  `LuaBehaviour` — C++-адаптер, владеющий таблицей экземпляра и прототипом;
  виртуальные вызовы идут через `sol::protected_function` в Lua.
- `uzlezz.behaviour {...}` создаёт прототип, расширяющий привязанную C++-базу
  через метатаблицу. Каждый экземпляр получает свою таблицу с `entity` и `world`.
  Необъявленный callback — пустая операция. Некорректный callback отвергается.
- `ScriptComponent` содержит только сериализуемые данные, без Lua-ссылок.
- `PrefabManager` создаёт JSON-компоненты, откатывает неудачный spawn и сохраняет поля.
- `core.lua` отвечает за HP/импульс, `enemy.lua` — движение/урон/анимацию,
  `waves.lua` — волны, ввод, статус и поражение. Решений механики в C++ нет.

Пример поведения:

```lua
Mover = uzlezz.behaviour { fields = { speed = 1.0 } }
function Mover:on_update(dt)
    local p = self.entity:get_position()
    p.x = p.x + self.entity:get_field("speed") * dt
    self.entity:set_position(p)
end
```

API: Vec3, Entity (position/yaw/tag/animation speed/fields), World
(input_pressed/find_by_tag/find_all_by_tag/spawn_prefab/destroy/set_status).
`find_all_by_tag` возвращает Lua-таблицу с индексацией от 1 для `ipairs` и `#`.

`fields` задаёт типы/defaults, JSON — переопределения. Поддержаны boolean,
int32, double, string; Lua различает `1` и `1.0`, тип сохраняется при обмене.
Inspector сохраняет параметры в исходный префаб. Play snapshot сохраняет
изменения сцены до Play, Stop восстанавливает их; runtime-значения не записываются.

## Владение, reload и ограничения

Entity — handle, а не указатель на компонент. Проверяются weak lifetime мира,
поколение ID и существование. Vec3 передаётся копией. World proxy инвалидируется
при Stop и не активируется повторно. Python-кода и привязок pybind11 больше нет.
Все вызовы Lua/ECS выполняются на главном потоке. Destroy применяется после update.
В on_destroy при Stop World API уже недоступен: нельзя создавать объекты при teardown.

Reload вне Play выполняет файл заново в отдельном environment и проверяет
классы/типы полей до замены. При ошибке прежние классы сохраняются. Проверяются
прикреплённые скрипты; preview врага включает Enemy в проверку. Неизвестный будущий
префаб проверяется при spawn. Runtime-ошибка отключает экземпляр до следующего Play.

Lua GC выполняется при Stop/Reload и инкрементально во время Play. Скрипты
доверенные: лимитов времени и памяти нет, намеренный бесконечный цикл заблокирует
главный поток. package/require, io/os/debug не открыты, но это не полноценный sandbox;
запись в `_G` может пережить reload. Не храните игровое состояние в `_G`.
Сохранение всей сцены, Python-совместимость, события и расширенный Inspector не входят в минимум.

## Проверки

`ScriptSystemTests`: реальная Lua VM и игровые скрипты, spawn/движение/урон/импульс,
30 минут симулированного времени, focus-gating, 100 циклов Stop/Reload/Play,
виртуальные callbacks, invalid generation/lifetime/World handles, syntax/runtime
errors, новый код после reload, валидация прототипа, сохранение/повторная загрузка JSON.
Остальные CTest-наборы проверяют регрессии ЛР 1 и GPU skinning.
Ручная визуальная проверка новой сцены и длительная реальная игровая сессия —
отдельно от автоматического теста с симулированным временем.

### Проверка Lua-версии 06.10.2026

- Чистая Windows x64 Release-сборка в `build/lab2-lua` успешна.
- CTest: **7/7 passed**, 5.41 секунды; ScriptSystemTests — 2.23 секунды.
- Smoke-запуск GameEngine из Release с `--stress-seconds 3`: exit code 0,
  в логе `Lua runtime initialized: Lua 5.4.8`, `Lua runtime shut down`,
  `Shutdown complete`, без ERROR. Это проверка запуска/завершения, не ручное демо.
- `dumpbin /dependents GameEngine.exe`: нет Python DLL или Lua DLL.
- `git diff --check`: ошибок whitespace нет.

Журналы в игнорируемом каталоге сборки:
`build/lab2-lua/Testing/Temporary/LastTest.log`,
`build/lab2-lua/Release/engine.log`, `build/lab2-lua/Release/lua-startup-smoke.json`.
ERROR в ScriptSystemTests ожидаемы: тест специально вводит ошибки и проверяет восстановление.
