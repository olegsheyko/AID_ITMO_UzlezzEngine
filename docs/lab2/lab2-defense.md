# ЛР 2 «Скриптинг» — подготовка к защите

Uzlezz Engine · АИД · ИТМО 2026 · промежуточное демо 09.10 · приёмка 16.10.

Текстовая версия интерактивной презентации [`lab2-defense.html`](lab2-defense.html): та же теория, разборы схем, настоящий код с пояснениями к строкам и все вопросы «Могут спросить». Сценарий показа — также в [DEMO.md](DEMO.md), сборка и архитектура — в [README.md](README.md).

## Оглавление

**Задача**
1. [Коротко: что сделано](#1-коротко-что-сделано)
2. [ТЗ и приёмка](#2-тз-и-приёмка)

**Встраивание**
3. [Карта подсистемы](#3-карта-подсистемы)
4. [Жизнь Lua state](#4-жизнь-lua-state)
5. [Вызов C++ → Lua → C++](#5-вызов-c--lua--c)
6. [Граница C++ ↔ Lua](#6-граница-c--lua)
7. [Кадр скриптов](#7-кадр-скриптов)

**Данные**
8. [Префабы и поля](#8-префабы-и-поля)
9. [Механика на Lua](#9-механика-на-lua)

**Допфичи**
10. [Hot reload через job system](#10-hot-reload-через-job-system)
11. [Reload в Play: L1 и L2](#11-reload-в-play-l1-и-l2)
12. [Sandbox: время и память](#12-sandbox-время-и-память)
13. [Tracy и подсказки в IDE](#13-tracy-и-подсказки-в-ide)
14. [Стабильность](#14-стабильность)

**Защита**
15. [Демо и правки правил](#15-демо-и-правки-правил)

**Лекции**
16. [Лекция 5, часть 1: объектная модель, ссылки, обновление](#16-лекция-5-часть-1-объектная-модель-ссылки-обновление)
17. [Лекция 5, часть 2: события и скриптинг](#17-лекция-5-часть-2-события-и-скриптинг)
18. [Лекции 1–4 кратко](#18-лекции-14-кратко)

**Справка**
19. [Lua для C++-программиста](#19-lua-для-c-программиста)
20. [Шпаргалка: цифры и где что лежит](#20-шпаргалка-цифры-и-где-что-лежит)

---

## 1. Коротко: что сделано

**Вся механика волн живёт в Lua — и меняется прямо посреди игры.**

Lua 5.4.8 и sol2 3.3.1 встроены в Uzlezz Engine как подсистема. Один `sol::state` на весь редактор; его создаёт и закрывает `ScriptSystem`. Lua-класс расширяет C++-базу `ScriptBehaviour` через адаптер-трамплин `LuaBehaviour`. Скрипт видит движок через хэндлы, а не указатели, и через четыре группы API: сущности и трансформ, ввод, спавн и уничтожение, поиск в мире. Механика «защита ядра от волн» целиком в трёх Lua-файлах; враги спавнятся из JSON-префаба; поля поведения правятся в инспекторе и сохраняются в префаб. Hot reload работает и в Play: файл проверяется задачей job system, применяется на главном потоке, состояние переносится (L2) или сбрасывается (L1). Сломанный или зациклившийся скрипт не роняет движок: в логе — файл:строка.

| Показатель | Значение |
|---|---|
| Механика целиком на Lua | 3 файла: `core.lua`, `enemy.lua`, `waves.lua`; в C++ ни одного правила |
| От ⌘S до нового кода в Play | меньше секунды: вотчер на воркере, без Stop |
| Падений от ошибок скрипта | 0: файл:строка в логе, движок жив |
| Допфичи | 5: hot reload через job system, hot reload в Play (L1/L2), sandbox, Tracy, подсказки в IDE |

## 2. ТЗ и приёмка

**ЛР проверяет два умения: провести границу «C++ / скрипты» и сделать итерацию над геймплеем живой.**

| Пункт | Требование ТЗ | Статус | Чем закрыто |
|---|---|---|---|
| 2.1 | Язык как подсистема: init/shutdown, единая точка загрузки, понятное время жизни state | есть | `ScriptSystem` внутри редактора, один `sol::state` — [раздел 3](#3-карта-подсистемы), [4](#4-жизнь-lua-state) |
| 2.1 | Дистрибуция интерпретатора | есть | Lua собрана из исходников внутрь exe |
| 2.2 | Движок → скрипт: entry points, класс поведения от C++-базы (trampoline) | есть | `ScriptBehaviour` → адаптер `LuaBehaviour` — [раздел 5](#5-вызов-c--lua--c) |
| 2.2 | Скрипт → движок: ≥ 4 категории API, владение без висячих ссылок | есть | Entity/World API, хэндл с поколением — [раздел 6](#6-граница-c--lua) |
| 2.3 | Hot reload без перезапуска движка | сверх | вотчер на job system; reload и в Edit, и в Play — [раздел 10](#10-hot-reload-через-job-system) |
| 2.4 | Механика целиком на скриптах, правило меняется по просьбе | есть | волны, враг, ядро — три Lua-файла — [раздел 9](#9-механика-на-lua) |
| 2.5 | Шаблонный спавн, поля поведения в данных | есть | JSON-префабы, `world:spawn_prefab` — [раздел 8](#8-префабы-и-поля) |
| 2.6 | Правка полей в редакторе с сохранением | есть | Inspector → Script → Save to Prefab |
| 3 | Сломанный скрипт, серия reload, длинная сессия, корректный выход | есть | файл:строка, лимиты, тесты на 30 минут и 200 reload — [раздел 14](#14-стабильность) |
| 4 | Допфичи | 5 | hot reload через job system и в Play (L1 + L2), sandbox, Tracy, IDE |

**Баллы.** Наличие и полнота — 8 (демо вживую: подсистема, bindings, reload, механика, префабы, поля). Стабильность — 4 (падение на ошибке скрипта = минус блок). Допфичи — 4 (любая работа сверх обязательного). Вовремя — +2 (демо в свой день, 16.10).

> **Живой критерий:** преподаватель просит поменять правило (число, условие, порядок). Правим `.lua`, сохраняем — изменение видно в игре без перезапуска. Заготовки — в [разделе 15](#15-демо-и-правки-правил).

Отвечают двое, кого назовёт преподаватель, не презентатор. Вопросы по всему коду команды и лекциям; каждый ответ — 0, 1 или 2 балла.

### Могут спросить

**Почему Lua, а не Python или C#?**

Lua создан для встраивания: ~200 КБ, ~15 тыс. строк ANSI C. Собираем из исходников внутрь exe — ставить нечего. Нет GIL, простой C API; sol2 (header-only) даёт usertypes, protected calls и environments. Python embeddable — ≈ 10 МБ рядом с exe, C# — десятки МБ рантайма. Минусы Lua: нет классов (делаем метатаблицами), индексы с 1, узкая экосистема.

**Что будет на машине пользователя?**

Exe, папка `assets` и `licenses`. Lua внутри exe, в системе ничего не ищется — изоляция как у embeddable-пакета Python, только размер — сотни КБ. Остальное как до ЛР 2: на Windows DLL Assimp и VC++ Runtime, на Mac — GLFW и Assimp из Homebrew. `setup_lua.sh`/`.ps1` нужны только для сборки: качают закреплённые исходники и проверяют SHA-256.

**Чем механика отличается от «скрипт-обёртки над C++»?**

В C++ нет ни одного правила: когда начать волну, сколько врагов, куда идти, сколько урона, когда импульс, когда поражение — всё в Lua. C++ даёт сервисы: ввод, поиск по тегу, спавн, уничтожение, трансформ, анимацию, строку HUD.

**Что будет, если правило попросят поменять во время игры?**

Сохраняем `.lua` — вотчер подхватит файл, и экземпляры перейдут на новый код без Stop. Режим L2 сохранит номер волны и HP ядра, L1 начнёт состояние заново.

## 3. Карта подсистемы

**У скриптинга один хозяин: редактор создаёт подсистему, кадр её вызывает, выход закрывает.**

```
EditorContext ── владеет ──► ScriptSystem::Impl
     │                      ┌──────────────────────────────────────────────┐
  владеет                   │ Sandbox      sol::state     модуль uzlezz    │
     ▼                      │ classes      instances      ApiState         │
World (ECS) ◄── хэндлы ──── │ LuaBehaviour run·extract    error·stats      │
     ▲                      └───────────▲───────────────────────┬──────────┘
   поля                                 │ drain → hotReload      │ spawn / ввод
     │                          ScriptWatcher ── задача ──► JobSystem (enkiTS)
Inspector · Gameplay                    │ читает                 │
     │ Save                    assets/scripts/*.lua       PrefabManager ─ читает ─► assets/prefabs/*.json
     ▼                                                    InputManager
assets/prefabs/*.json
```

| Блок | Кто / где живёт | Что делает | Файл |
|---|---|---|---|
| EditorContext | главный поток | Владеет ScriptSystem и World. Play: снимок сцены → `scripts.start()`. Кадр: `scripts.update` до физики. Stop: `scripts.stop()` → снимок обратно. Здесь же применяются изменения от вотчера. | `src/editor/EditorContext.cpp` |
| World (ECS) | главный поток | Таблица на каждый тип компонента; Entity — id с поколением и токеном жизни мира. ScriptComponent — только данные: путь, класс, префаб, поля. | `src/ecs/World.h`, `src/scripting/ScriptComponent.h` |
| PrefabManager | главный поток | `spawn`: разбор JSON, компоненты, откат при ошибке. `saveFields`: поля обратно в JSON через `.tmp` + rename. | `src/prefabs/PrefabManager.cpp` |
| assets/prefabs/*.json | данные | Шаблоны arena, core, waves, enemy: компоненты и значения полей поведения. | `assets/prefabs/` |
| Inspector · Gameplay | главный поток (ImGui) | Inspector: карточка Script, виджет по типу поля, Save to Prefab. Gameplay: Open Arena, Reload Scripts, «Reload on save», режим L1/L2, куча и время скриптов. | `src/editor/panels/InspectorPanel.cpp`, `GameplayPanel.cpp` |
| Sandbox | ScriptSystem::Impl | Потолок кучи 256 МБ и бюджет 100 мс на вызов. Объявлен первым — переживает `sol::state`. | `ScriptSystem.cpp`, `struct Sandbox` |
| sol::state | ScriptSystem::Impl | Один Lua state на редактор: `lua_newstate` с нашим аллокатором, библиотеки base/math/table/string, count-хук, обработчики ошибок. | `ScriptSystem.cpp`, `Impl()` |
| модуль uzlezz | в state | Usertypes ScriptBehaviour, Vec3, Entity, World и функция `uzlezz.behaviour` — всё API скрипта. | `ScriptSystem.cpp`, `bindEngine` |
| classes | в state | Таблицы классов поведения. Каждый файл выполняется в своём environment; класс проверяется при загрузке. | `ScriptSystem.cpp`, `run` / `extract` |
| instances | только в Play | Для каждой сущности со скриптом: C++-адаптер LuaBehaviour, таблица `self`, снимок `initial` для L2, флаг `failed`. | `ScriptSystem.cpp`, `Impl::Instance` |
| ApiState | только в Play | `active`, `allowInput`, сервисы `spawn` и `pressed`, набор на удаление, строка статуса. Новый на каждом Stop — старые прокси протухают. | `ScriptSystem.cpp`, `ApiState`, `WorldApi` |
| LuaBehaviour | C++ | Наследник ScriptBehaviour: виртуальный `on_update` → protected call в Lua с бюджетом и зоной Tracy. | `ScriptSystem.cpp`, `class LuaBehaviour` |
| run · extract | главный поток | Единая точка загрузки: файл или текст от вотчера выполняется в новом environment, `extract` проверяет класс, поля и callbacks. | `ScriptSystem.cpp`, `Impl::run` |
| error · stats | главный поток | Первая строка ошибки — в окно Gameplay, полный текст со стеком — в лог. `stats`: куча, пик, экземпляры, мс за кадр. | `ScriptSystem.cpp`, `report`, `stats` |
| ScriptWatcher | воркер job system | Раз в 0,2 с ставит фоновую задачу: обход `assets/scripts`, метки файлов, debounce, чтение и проверка синтаксиса. Результат забирает главный поток. | `src/scripting/ScriptWatcher.cpp` |
| JobSystem (enkiTS) | пул воркеров | `submitBackground`: закреплённая задача Low — главный поток её не крадёт, кадр не ждёт диска и компиляции. | `src/jobs/JobSystem.cpp` |
| assets/scripts/*.lua | данные | `core.lua`, `enemy.lua`, `waves.lua` — вся механика. | `assets/scripts/` |
| InputManager | главный поток | Actions StartWave = Space, DefensePulse = F; `isActionPressed` — фронт нажатия. Скрипт видит ввод только при фокусе окна Game. | `src/input/InputManager.cpp` |

### Могут спросить

**Где единая точка загрузки и выполнения скриптов?**

Загрузка — `Impl::run(path, source)`: файл или текст выполняется в новом `sol::environment` под бюджетом времени, затем `extract` проверяет класс. Выполнение — любой entry point идёт через `LuaBehaviour::call`: protected call, traceback, бюджет, зона Tracy.

**Почему одна VM на весь редактор, а не на каждую сущность?**

Скрипты доверенные и маленькие; одна VM — один GC, общий модуль `uzlezz`, классы грузятся один раз. Изоляцию по времени и памяти даёт Sandbox. Отдельный state на мод/сущность — следующий шаг, если появятся чужие скрипты.

**Почему ScriptSystem объявлен после World в EditorContext?**

Члены класса разрушаются в обратном порядке: ScriptSystem умирает раньше мира. Его `stop()` зовёт `on_destroy` и трогает компоненты — мир в этот момент ещё жив.

**Кто вызывает скрипты каждый кадр?**

`EditorContext::updateGameplay` в режиме Play зовёт `scripts.update(dt, allowInput)` — до физики и анимации. В Edit скрипты не тикают, работает только вотчер.

**Где тут job system?**

Только в вотчере: обход папки, метки файлов, чтение и проверка синтаксиса — задачей `submitBackground` на воркере. Всё, что трогает Lua state и ECS, — на главном потоке.

## 4. Жизнь Lua state

**Lua state живёт столько же, сколько редактор; Play и Stop пересоздают только экземпляры.**

Восемь шагов: что происходит и что в этот момент лежит в state.

| # | Фаза | Шаг | Что есть в state |
|---|---|---|---|
| 1 | старт | **Редактор создан.** `EditorContext` создаёт `ScriptSystem`: сначала Sandbox, потом state через `lua_newstate` с нашим аллокатором. | Sandbox, sol::state |
| 2 | старт | **Настройка state.** Хук бюджета, четыре библиотеки, обработчик исключений, `print` в лог, без `dofile`, модуль `uzlezz`. | + библиотеки и `uzlezz` |
| 3 | Edit | **Open Arena.** `reload()` выполняет файлы и проверяет классы; подмена — только если всё загрузилось. | + classes |
| 4 | Play | **Play.** `start()` = stop + reload с диска + включить World API + экземпляр каждой сущности со скриптом. | + instances, ApiState active |
| 5 | Play | **Экземпляр.** Своя таблица с `entity` и `world`, метатаблица на класс, `on_create`, снимок для L2. | всё |
| 6 | Stop | **Stop.** World API выключен, у всех `on_destroy`, новый ApiState — старые прокси протухли. | instances и ApiState ушли |
| 7 | Stop | **Сборка мусора.** Таблицы экземпляров уходят из кучи; классы и VM живут до следующего Play. | Sandbox, state, библиотеки, classes |
| 8 | выход | **Выход.** `~ScriptSystem`: stop, затем `lua_close` — после всех ссылок sol2; Sandbox умирает последним. | ничего |

`src/scripting/ScriptSystem.cpp`, строки 299–327, 459–463:

```cpp
struct ScriptSystem::Impl {
    Sandbox sandbox; // аллокатор state'а: объявлен раньше и переживает его
    // Declared before every sol reference, destroyed after them.
    sol::state lua{&sol::default_at_panic, &allocate, &sandbox};
    World& world;
    std::shared_ptr<ApiState> api = std::make_shared<ApiState>();
    std::map<std::string, sol::table> classes;
    struct Instance {
        EntityHandle entity;
        std::unique_ptr<LuaBehaviour> behaviour;
        bool failed = false;
        sol::table initial; // self.* сразу после on_create — чтобы при hot reload понять, что поменяли в коде
    };
    std::map<Entity, Instance> instances;
    std::string error;
    sol::reference errorHandler;
    double updateMs = 0.0;
    explicit Impl(World& w) : world(w) {
        api->world = &w;
        lua_sethook(lua.lua_state(), &budgetHook, LUA_MASKCOUNT, 1000);
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);
        lua.set_exception_handler(&locatedException);
        lua.set_function("print", &print);
        // Скрипты не читают файлы в обход движка.
        lua["dofile"] = sol::lua_nil;
        lua["loadfile"] = sol::lua_nil;
        errorHandler = sol::make_reference(lua.lua_state(), &traceback);
        bindEngine(lua);
        LOG_INFO("Lua runtime initialized: " LUA_RELEASE " (sol2, statically linked)");
// ⋯
ScriptSystem::ScriptSystem(World& world) : impl_(std::make_unique<Impl>(world)) {}
ScriptSystem::~ScriptSystem() {
    stop();
    impl_.reset(); // sol::state closes after instances, prototypes and environments.
    LOG_INFO("Lua runtime shut down");
```

1. **стр. 300** `Sandbox sandbox; // аллокатор state'а: объявлен раньше и переживает его` — **Sandbox** объявлен первым: это аллокатор state'а, он должен пережить сам state.
2. **стр. 302** `sol::state lua{&sol::default_at_panic, &allocate, &sandbox};` — Один `sol::state` на весь редактор. Создан через `lua_newstate` с нашим аллокатором.
3. **стр. 305** `std::map<std::string, sol::table> classes;` — Классы по ключу `путь:Класс` — загружаются один раз на ключ.
4. **стр. 312** `std::map<Entity, Instance> instances;` — Экземпляры живут только в Play: Stop их удаляет, VM остаётся.
5. **стр. 318** `lua_sethook(lua.lua_state(), &budgetHook, LUA_MASKCOUNT, 1000);` — Count-хук раз в 1000 инструкций — бюджет времени ([раздел 12](#12-sandbox-время-и-память)).
6. **стр. 319** `lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);` — Только base, math, table, string. Нет io, os, package, debug, coroutine.
7. **стр. 320** `lua.set_exception_handler(&locatedException);` — Свой обработчик исключений sol2: ошибки из C++-биндингов получают файл:строка.
8. **стр. 323** `lua["dofile"] = sol::lua_nil;` — Закрыли чтение файлов в обход движка.
9. **стр. 326** `bindEngine(lua);` — Модуль `uzlezz`: usertypes и функция `uzlezz.behaviour`.
10. **стр. 462** `impl_.reset(); // sol::state closes after instances, prototypes and environments.` — Выход: сначала `stop()`, потом `lua_close` — уже после всех ссылок sol2.

`src/scripting/ScriptSystem.cpp`, строки 503–521:

```cpp
bool ScriptSystem::reload() {
    ZoneScopedN("Lua: reload");
    auto& p = *impl_;
    if (running()) { p.error = "Stop Play before reloading scripts"; return false; }
    try {
        std::map<std::string, sol::table> replacement;
        std::map<Entity, ScriptComponent> components;
        for (Entity e : p.world.getEntities()) if (p.world.hasComponent<ScriptComponent>(e)) {
            auto c = p.world.getComponent<ScriptComponent>(e); const auto k = p.key(c);
            if (!replacement.count(k)) replacement[k] = p.load(c);
            p.defaults(c, replacement[k]); components.emplace(e, std::move(c));
        }
        p.classes.swap(replacement);
        for (auto& [e,c] : components) p.world.getComponent<ScriptComponent>(e) = std::move(c);
        p.error.clear();
    } catch (const std::exception& x) { p.report(x); return false; }
    p.lua.collect_garbage();
    return true;
}
```

1. **стр. 506** `if (running()) { p.error = "Stop Play before reloading scripts"; return false; }` — Полный reload — только вне Play; в Play работает `hotReload`.
2. **стр. 508** `std::map<std::string, sol::table> replacement;` — Новые классы собираются в отдельную карту…
3. **стр. 512** `if (!replacement.count(k)) replacement[k] = p.load(c);` — …файл выполняется в новом environment, класс проверяется.
4. **стр. 515** `p.classes.swap(replacement);` — Подмена только если всё загрузилось: при ошибке старые классы остаются.
5. **стр. 518** `} catch (const std::exception& x) { p.report(x); return false; }` — Ошибка: первая строка с файл:строка — в UI, полный текст — в лог.

`src/scripting/ScriptSystem.cpp`, строки 604–621:

```cpp
bool ScriptSystem::start() {
    ZoneScopedN("Lua: start");
    stop();
    if (!reload()) return false;
    auto& p = *impl_; p.api->active = true; p.api->spawn = spawnPrefab; p.api->pressed = inputPressed;
    for (Entity e : p.world.getEntities()) if (p.world.hasComponent<ScriptComponent>(e)) {
        try { p.create(e); } catch (const std::exception& x) { p.report(x); stop(); return false; }
    }
    return true;
}
void ScriptSystem::stop() {
    auto& p = *impl_;
    if (!p.api->active && p.instances.empty()) return;
    p.api->active = false; // Retained World proxies cannot spawn during teardown.
    while (!p.instances.empty()) p.remove(p.instances.begin()->first);
    p.api = std::make_shared<ApiState>(); p.api->world = &p.world;
    p.lua.collect_garbage();
}
```

1. **стр. 607** `if (!reload()) return false;` — Play всегда перечитывает файлы с диска: `start()` = stop + reload.
2. **стр. 608** `auto& p = *impl_; p.api->active = true; p.api->spawn = spawnPrefab; p.api->pressed = in…` — World API включается только на время Play.
3. **стр. 610** `try { p.create(e); } catch (const std::exception& x) { p.report(x); stop(); return fals…` — Экземпляр и `on_create` для каждой сущности со скриптом. Ошибка — Play отменяется.
4. **стр. 617** `p.api->active = false; // Retained World proxies cannot spawn during teardown.` — Сначала выключаем World API: в `on_destroy` при Stop спавнить уже нельзя.
5. **стр. 618** `while (!p.instances.empty()) p.remove(p.instances.begin()->first);` — `on_destroy` у каждого экземпляра.
6. **стр. 619** `p.api = std::make_shared<ApiState>(); p.api->world = &p.world;` — Новый ApiState: старые `self.world` протухают навсегда.
7. **стр. 620** `p.lua.collect_garbage();` — Полная сборка мусора: таблицы экземпляров уходят из кучи.

`src/scripting/ScriptSystem.cpp`, строки 368–374, 438–449:

```cpp
sol::table makeObject(const sol::table& cls, Entity e) {
    sol::table object = lua.create_table();
    object[sol::metatable_key] = lua.create_table_with("__index", cls);
    object["entity"] = handle(world, e);
    object["world"] = WorldApi{api};
    return object;
}
// ⋯
void create(Entity e) {
    auto& c = world.getComponent<ScriptComponent>(e);
    const auto k = key(c);
    if (!classes.count(k)) classes[k] = load(c);
    defaults(c, classes.at(k));
    sol::table object = makeObject(classes.at(k), e);
    auto behaviour = std::make_unique<LuaBehaviour>(object, classes.at(k), errorHandler, sandbox, c.className);
    LuaBehaviour* entry = behaviour.get();
    auto& instance = instances.emplace(e, Instance{handle(world,e), std::move(behaviour), false, {}}).first->second;
    entry->on_create();
    instance.initial = snapshot(object);
}
```

1. **стр. 370** `object[sol::metatable_key] = lua.create_table_with("__index", cls);` — Экземпляр — своя таблица; методы ищутся в классе через `__index`.
2. **стр. 371** `object["entity"] = handle(world, e);` — Сущность — хэндл, а не указатель.
3. **стр. 372** `object["world"] = WorldApi{api};` — Прокси мира со слабой ссылкой на ApiState.
4. **стр. 441** `if (!classes.count(k)) classes[k] = load(c);` — Класс грузится один раз на ключ — например, при первом спавне врага.
5. **стр. 442** `defaults(c, classes.at(k));` — Поля компонента приводятся к объявлению класса.
6. **стр. 447** `entry->on_create();` — Виртуальный `on_create` уходит в Lua.
7. **стр. 448** `instance.initial = snapshot(object);` — Снимок `self.*` после `on_create` — пригодится для L2.

### Могут спросить

**Кто создаёт Lua state и когда он умирает?**

`ScriptSystem::Impl` в конструкторе редактора; умирает в `~ScriptSystem` при закрытии редактора. Stop уничтожает экземпляры, но не VM.

**Что лежит в state, а чего там нет?**

Есть: модуль `uzlezz`, классы (по environment на файл), таблицы экземпляров и их снимки. Нет: указателей на компоненты, io/os/package/debug/coroutine, игрового состояния в `_G` — запись туда переживёт reload, поэтому так не делаем.

**Почему Sandbox объявлен раньше sol::state?**

Это аллокатор state'а: при `lua_close` Lua освобождает всю кучу через него. Члены разрушаются в обратном порядке — Sandbox должен умереть последним.

**Почему Play перечитывает файлы с диска?**

`start()` начинается с `reload()`: правка, сделанная в Edit при выключенном вотчере, всё равно попадёт в игру. Ошибка загрузки отменяет Play, сцена восстанавливается.

**Когда работает сборка мусора?**

Шаг инкрементальной сборки каждый кадр (`lua_gc(LUA_GCSTEP, 32)`), полная — при Stop, reload и hot reload. Кучу видно в окне Gameplay и на графике Tracy.

**Что если on_create падает при Play?**

Ошибка уходит в лог с файл:строка, `start()` вызывает `stop()` и возвращает false; редактор восстанавливает снимок сцены и остаётся в Edit.

## 5. Вызов C++ → Lua → C++

**Движок зовёт виртуальный `on_update`, а выполняется Lua: адаптер-трамплин `LuaBehaviour`.**

### Путь одного вызова `Enemy:on_update`

```
 C++ движок                 LuaBehaviour · sol2              Lua VM
 ──────────                 ───────────────────              ──────
 1 update → on_update(dt) ↺
 2 ── virtual on_update ──────────►
                          3 зона · handler · Deadline ↺
                          4 ── pcall(self, dt) ─────────────────►
 5 ◄──────────────────────────────── self.entity:get_position() ─
 6 ── Vec3 копией ─────────────────────────────────────────────────►
                          7 ◄──── ok или ошибка → исключение ────
```

1. **Цикл кадра.** `ScriptSystem::update` зовёт виртуальный `on_update(dt)` у C++-объекта.
2. **Трамплин.** `LuaBehaviour` переопределяет метод и ищет `"on_update"` в таблице класса.
3. **Подготовка.** Зона Tracy с именем, traceback-обработчик, бюджет 100 мс.
4. **Lua.** Protected call: выполняется `Enemy:on_update(dt)`, `self` — таблица экземпляра.
5. **Обратно в C++.** `self.entity:get_position()` — метод usertype; sol2 вызывает лямбду, она проверяет хэндл.
6. **Результат.** Позиция пришла копией (Vec3), Lua считает шаг и пишет позицию через `set_position`.
7. **Ошибка?** Невалидный результат → исключение → `update` помечает экземпляр failed, остальные работают.

`src/scripting/ScriptSystem.cpp`, строки 179–212:

```cpp
class LuaBehaviour final : public ScriptBehaviour {
public:
    LuaBehaviour(sol::table object, sol::table prototype, sol::reference handler, Sandbox& sandbox, const std::string& className)
        : object_(std::move(object)), prototype_(std::move(prototype)), handler_(std::move(handler)), sandbox_(sandbox),
          labels_{className + ":on_create", className + ":on_update", className + ":on_destroy", className + ":on_reload"} {}
    void on_create() override { call(0, "on_create"); }
    void on_update(float dt) override { call(1, "on_update", dt); }
    void on_destroy() override { call(2, "on_destroy"); }
    // Hot reload в Play: новый экземпляр сам переносит нужное из старого (необязательный callback).
    void on_reload(const sol::table& previous) { call(3, "on_reload", previous); }
    bool has(const char* name) const { return prototype_.raw_get<sol::object>(name).get_type() == sol::type::function; }
    const sol::table& object() const { return object_; }
private:
    template<class... Args> void call(int slot, const char* name, Args... args) {
        sol::object method = prototype_.raw_get<sol::object>(name);
        if (!method.valid() || method == sol::lua_nil) return;
        // Зона на каждый entry point: на трейсе видно, сколько стоит тик Enemy, Waves и Core.
        ZoneScopedN("Lua callback");
        ZoneName(labels_[slot].data(), labels_[slot].size());
        sol::protected_function function = method.as<sol::protected_function>();
        function.set_error_handler(handler_);
        Deadline deadline(sandbox_);
        sol::protected_function_result result = function(object_, args...);
        if (result.valid()) return;
        // Для нехватки памяти Lua не зовёт обработчик ошибок, места в сообщении нет — называем вызов.
        if (result.status() == sol::call_status::memory)
            throw std::runtime_error(labels_[slot] + ": Lua heap limit exceeded (" + std::to_string(sandbox_.limit >> 20) + " MB)");
        sol::error error = result; throw error;
    }
    sol::table object_, prototype_;
    sol::reference handler_;
    Sandbox& sandbox_;
    std::string labels_[4];
};
```

1. **стр. 179** `class LuaBehaviour final : public ScriptBehaviour {` — Адаптер-трамплин: наследник C++-базы, за которым стоит Lua.
2. **стр. 185** `void on_update(float dt) override { call(1, "on_update", dt); }` — Движок зовёт виртуальный метод и не знает, что за ним Lua.
3. **стр. 193** `sol::object method = prototype_.raw_get<sol::object>(name);` — Метод берётся из самого класса. Нет метода — пустая операция.
4. **стр. 197** `ZoneName(labels_[slot].data(), labels_[slot].size());` — Зона Tracy с именем `Enemy:on_update`.
5. **стр. 199** `function.set_error_handler(handler_);` — Traceback-обработчик: в лог уйдёт стек Lua.
6. **стр. 200** `Deadline deadline(sandbox_);` — Бюджет 100 мс на этот вызов.
7. **стр. 201** `sol::protected_function_result result = function(object_, args...);` — Protected call, `self` = таблица экземпляра.
8. **стр. 204** `if (result.status() == sol::call_status::memory)` — Нехватка памяти: Lua не зовёт обработчик — имя вызова добавляем сами.
9. **стр. 206** `sol::error error = result; throw error;` — Ошибка → исключение; `update` отключит этот экземпляр.

`src/scripting/ScriptSystem.cpp`, строки 221–224:

```cpp
api.new_usertype<EntityHandle>("Entity", sol::no_constructor,
    "is_alive", &EntityHandle::valid,
    "id", sol::property([](const EntityHandle& h) { h.check(); return h.id; }),
    "get_position", [](const EntityHandle& h) { h.check(); return h.world->getComponent<Transform>(h.id).position; },
```

1. **стр. 221** `api.new_usertype<EntityHandle>("Entity", sol::no_constructor,` — Usertype: sol2 строит метатаблицу, Lua видит методы через двоеточие.
2. **стр. 224** `"get_position", [](const EntityHandle& h) { h.check(); return h.world->getComponent<Tra…` — Лямбда на C++: `check()` хэндла, затем копия позиции уходит в Lua как Vec3.

### Где ищется метод: цепочка метатаблиц

```
self (таблица экземпляра)          entity · world · cooldown
   │ метатаблица {__index = Core}
   ▼
Core (класс из core.lua)           fields · base · on_create · on_update
   │ метатаблица {__index = uzlezz.ScriptBehaviour}
   ▼
uzlezz.ScriptBehaviour (usertype)  on_create · on_update · on_destroy  (C++)
```

| Скрипт пишет | Где найдено | Пояснение |
|---|---|---|
| `self.cooldown` | экземпляр | Состояние живёт в `self`. |
| `self.entity` | экземпляр | Хэндл сущности кладёт `makeObject` при создании экземпляра. |
| `self:on_update` | класс Core | В `self` нет → `__index` ведёт в класс. Так движок и вызывает его через LuaBehaviour. |
| `self.fields` | класс Core | Объявление полей лежит в классе; значения — в ScriptComponent, читать через `get_field`. |
| `self.base` | класс Core | `uzlezz.behaviour` записал `base = uzlezz.ScriptBehaviour` — по нему `extract` проверяет «наследование». |
| `self:on_destroy` | C++-база | В Core нет → дошли до C++-базы, там пустой метод. Но движок через `raw_get` его не вызовет: класс не объявил `on_destroy`. |
| `self.health` | нигде → `nil` | Поле `health` не в `self`, а в ScriptComponent: `self.entity:get_field("health")`. |

> **Важная тонкость.** `LuaBehaviour::call` берёт метод через `raw_get` только из класса. Если `Core` не объявил `on_destroy`, движок ничего не вызывает — хотя через `__index` нашлась бы C++-заглушка базы.

Так устроен `uzlezz.behaviour`: прототип получает поле `base` и метатаблицу с `__index` на C++-базу. При загрузке проверяется, что `base == uzlezz.ScriptBehaviour`, `fields` — таблица, а callbacks — функции. Поля поведения (`health`, `speed`) в `self` не лежат: они в `ScriptComponent` и читаются через `get_field`, поэтому инспектор и префаб видят их без Lua.

### Как в pybind11 (лекция 5)

| Лекция 5: pybind11 | У нас: sol2 + Lua | Зачем |
|---|---|---|
| `class PyBehaviour` с `virtual OnUpdate()` | `class ScriptBehaviour` с `virtual on_update(float)` | C++-база, которую зовёт движок |
| `PyBehaviourTrampoline` + `PYBIND11_OVERRIDE` | `LuaBehaviour` + `call(slot, name)` | виртуальный вызов уходит в интерпретатор |
| `class MyPyBeh(gp.PyBehaviour)` | `Enemy = uzlezz.behaviour { … }` | класс скрипта «наследует» базу |
| `inspect.getmembers` ищет наследника | имя класса задано в префабе, проверяем `base` | движку не нужно угадывать класс |
| `py::error_already_set` с файлом и строкой | `protected_function` + traceback-обработчик | ошибка скрипта не роняет движок |
| `module.reload()` + новый экземпляр | `hotReload` + новый объект, перенос L2 | правка без перезапуска |
| `pybind11-stubgen` | `tools/lua-stubs/uzlezz.lua` + тест сверки | подсказки в IDE |
| embeddable Python ≈ 9,6 МБ рядом с exe | Lua внутри exe | ничего не ставить пользователю |

### Могут спросить

**Где у вас trampoline (трамплин)?**

`LuaBehaviour` — final-наследник C++-базы `ScriptBehaviour`. Каждый override зовёт `call(slot, name)`: метод берётся из таблицы класса и вызывается protected call с `self` = таблица экземпляра. Движок не знает, что за виртуальным методом Lua.

**Что если в классе нет on_update?**

Пустая операция: `raw_get` вернёт nil, `call` выйдет сразу. Это как обычный, не pure virtual метод в pybind11.

**Зачем protected_function, а не обычный вызов?**

Ошибка Lua внутри protected call возвращается как невалидный результат, а не через longjmp сквозь C++. Обработчик добавляет стек, мы превращаем результат в исключение и отключаем один экземпляр.

**Как ошибка из C++-биндинга получает файл:строка?**

sol2 по умолчанию кладёт в Lua только `what()`. Мы зарегистрировали свой exception handler: он добавляет `luaL_where(L, 1)` — место вызова в Lua. Пример: «enemy.lua:13: Enemy: unknown field 'helth'».

**Почему Vec3 копией, а не ссылкой?**

Ссылка на `Transform::position` — указатель в хеш-таблицу компонентов; при добавлении компонента таблица может перестроиться. Копия безопасна; запись — явным `set_position` с проверкой хэндла и конечности чисел.

## 6. Граница C++ ↔ Lua

**Скрипт видит движок через хэндлы и четыре группы API — и не держит ни одного указателя.**

ТЗ просит не «две функции для галочки», а API, достаточное для механики, и продуманное владение на границе.

### API: четыре категории из ТЗ

| Категория | Методы |
|---|---|
| Сущность и трансформ | `is_alive`, `id`, `get_position`, `set_position`, `set_yaw`, `get_tag`, `set_animation_speed`, `get_field`, `set_field` |
| Ввод | `world:input_pressed("StartWave")` — только при фокусе окна Game |
| Спавн и уничтожение | `world:spawn_prefab(path, pos)`, `world:destroy(e)` — уничтожение отложенное |
| Запросы к миру | `world:find_by_tag(tag)`, `world:find_all_by_tag(tag)` — массив с 1 |
| Прочее | `world:set_status(text)` — строка HUD; `print` — в Console с файл:строка |

`src/scripting/ScriptSystem.cpp`, строки 213–282:

```cpp
void bindEngine(sol::state& lua) {
    auto api = lua.create_named_table("uzlezz");
    api.new_usertype<ScriptBehaviour>("ScriptBehaviour", sol::no_constructor,
        "on_create", &ScriptBehaviour::on_create, "on_update", &ScriptBehaviour::on_update,
        "on_destroy", &ScriptBehaviour::on_destroy);
    api.new_usertype<Vec3>("Vec3", sol::call_constructor,
        sol::factories([](float x, float y, float z) { return Vec3{x,y,z}; }),
        "x", &Vec3::x, "y", &Vec3::y, "z", &Vec3::z);
    api.new_usertype<EntityHandle>("Entity", sol::no_constructor,
        "is_alive", &EntityHandle::valid,
        "id", sol::property([](const EntityHandle& h) { h.check(); return h.id; }),
        "get_position", [](const EntityHandle& h) { h.check(); return h.world->getComponent<Transform>(h.id).position; },
        "set_position", [](const EntityHandle& h, Vec3 p) { h.check(); checkPosition(p); h.world->getComponent<Transform>(h.id).position = p; },
        "set_yaw", [](const EntityHandle& h, float yaw) {
            h.check(); if (!std::isfinite(yaw)) throw std::runtime_error("Yaw must be finite");
            h.world->getComponent<Transform>(h.id).rotation.z = yaw;
        },
        "get_tag", [](const EntityHandle& h) { h.check(); return h.world->hasComponent<Tag>(h.id) ? h.world->getComponent<Tag>(h.id).name : std::string{}; },
        "set_animation_speed", [](const EntityHandle& h, float speed) {
            h.check(); if (!std::isfinite(speed)) throw std::runtime_error("Speed must be finite");
            if (h.world->hasComponent<Animator>(h.id)) h.world->getComponent<Animator>(h.id).speed = speed;
        },
        "get_field", [](const EntityHandle& h, const std::string& name, sol::this_state state) {
            return toLua(state, fieldOf(scriptOf(h), name));
        },
        "set_field", [](const EntityHandle& h, const std::string& name, sol::object value) {
            auto& component = scriptOf(h);
            auto& old = fieldOf(component, name);
            auto next = fromLua(value);
            if (!coerce(next, old))
                throw std::runtime_error(component.className + ": field '" + name + "' expects " + typeName(old) + ", got " + typeName(next));
            old = next;
        });
    api.new_usertype<WorldApi>("World", sol::no_constructor,
        "input_pressed", [](const WorldApi& a, const std::string& action) {
            auto s = a.get(); return s->allowInput && s->pressed && s->pressed(action);
        },
        "find_by_tag", [](const WorldApi& a, const std::string& tag) {
            auto s = a.get();
            for (Entity e : s->world->getEntities())
                if (!s->destroyed.count(e) && s->world->hasComponent<Tag>(e) && s->world->getComponent<Tag>(e).name == tag)
                    return handle(*s->world, e);
            return EntityHandle{};
        },
        "find_all_by_tag", [](const WorldApi& a, const std::string& tag) {
            auto s = a.get(); std::vector<EntityHandle> result;
            for (Entity e : s->world->getEntities())
                if (!s->destroyed.count(e) && s->world->hasComponent<Tag>(e) && s->world->getComponent<Tag>(e).name == tag)
                    result.push_back(handle(*s->world, e));
            return sol::as_table(std::move(result));
        },
        "spawn_prefab", [](const WorldApi& a, const std::string& path, Vec3 position) {
            auto s = a.get(); checkPosition(position);
            if (!s->spawn) throw std::runtime_error("Prefab service unavailable");
            Entity e = s->spawn(path); auto h = handle(*s->world, e); h.check();
            s->world->getComponent<Transform>(e).position = position; return h;
        },
        "destroy", [](const WorldApi& a, const EntityHandle& h) {
            auto s = a.get(); h.check();
            if (h.world != s->world) throw std::runtime_error("Entity belongs to another world");
            s->destroyed.insert(h.id);
        },
        "set_status", [](const WorldApi& a, const std::string& text) { a.get()->status = text; });
    lua.script(R"(
        function uzlezz.behaviour(prototype)
            prototype.base = uzlezz.ScriptBehaviour
            return setmetatable(prototype, {__index = uzlezz.ScriptBehaviour})
        end
    )");
}
```

1. **стр. 215** `api.new_usertype<ScriptBehaviour>("ScriptBehaviour", sol::no_constructor,` — C++-база поведения видна в Lua как `uzlezz.ScriptBehaviour`.
2. **стр. 218** `api.new_usertype<Vec3>("Vec3", sol::call_constructor,` — Vec3 — значение: в Lua и обратно передаётся копией.
3. **стр. 222** `"is_alive", &EntityHandle::valid,` — **Сущность и трансформ.** `is_alive` не бросает, остальные методы сначала зовут `check()`.
4. **стр. 235** `"get_field", [](const EntityHandle& h, const std::string& name, sol::this_state state) {` — Поле поведения читается из ScriptComponent при каждом вызове.
5. **стр. 242** `if (!coerce(next, old))` — Тип поля сохраняется: приводим или бросаем ошибку с именем поля.
6. **стр. 247** `"input_pressed", [](const WorldApi& a, const std::string& action) {` — **Ввод.** Только когда окно Game в фокусе.
7. **стр. 250** `"find_by_tag", [](const WorldApi& a, const std::string& tag) {` — **Запросы к миру.** Помеченных на удаление не видно.
8. **стр. 264** `"spawn_prefab", [](const WorldApi& a, const std::string& path, Vec3 position) {` — **Спавн** через PrefabManager одной командой.
9. **стр. 270** `"destroy", [](const WorldApi& a, const EntityHandle& h) {` — **Уничтожение** отложенное — после прохода update.
10. **стр. 277** `function uzlezz.behaviour(prototype)` — «Наследование» Lua-класса от C++-базы: метатаблица с `__index`.

### Хэндл: что видит скрипт (симулятор)

Хэндл — это три числа `{ id, generation, токен мира }`, а не указатель. `valid()` делает три проверки: токен мира жив, сущность жива, поколение совпало.

| Действие | Токен жив | Сущность жива | Поколение совпало | `self.target:is_alive()` | `self.target:get_position()` |
|---|---|---|---|---|---|
| Сохранили врага #5 (поколение 7) в `self.target` | ✓ | ✓ | ✓ | `true` | `Vec3(…)` |
| Враг дошёл: `destroy` | ✓ | ✗ | ✗ | `false` | ошибка `enemy.lua:N: Stale EntityHandle` |
| Stop → снимок сцены | ✗ (токен сменился) | ✓ (#5 восстановлен) | ✗ (новое поколение) | `false` | ошибка `Stale EntityHandle` |

- `World::destroyEntity` стирает поколение: проверка «жива» не проходит, указателя не было — падать нечему.
- Stop: `world.clear()` меняет токен, снимок возвращает сущности с теми же id и новыми поколениями. Старый хэндл не оживёт, даже если #5 снова есть.
- После Stop старый `self.world` тоже недействителен: `find_by_tag` даст «Script world is no longer active».

`src/scripting/ScriptSystem.cpp`, строки 15–25, 34–41:

```cpp
struct EntityHandle {
    World* world = nullptr;
    std::weak_ptr<int> lifetime;
    Entity id = kInvalidEntity;
    std::uint64_t generation = 0;
    bool valid() const {
        return !lifetime.expired() && world && world->isAlive(id) && world->generation(id) == generation;
    }
    void check() const { if (!valid()) throw std::runtime_error("Stale EntityHandle"); }
};
EntityHandle handle(World& w, Entity e) { return {&w, w.lifetime(), e, w.generation(e)}; }
// ⋯
struct WorldApi {
    std::weak_ptr<ApiState> state;
    std::shared_ptr<ApiState> get() const {
        auto s = state.lock();
        if (!s || !s->active) throw std::runtime_error("Script world is no longer active");
        return s;
    }
};
```

1. **стр. 17** `std::weak_ptr<int> lifetime;` — Токен жизни мира: `world.clear()` его меняет — все старые хэндлы протухают.
2. **стр. 19** `std::uint64_t generation = 0;` — Поколение: id может повториться, поколение — нет.
3. **стр. 20** `bool valid() const {` — Три проверки: мир жив, сущность жива, поколение совпало.
4. **стр. 23** `void check() const { if (!valid()) throw std::runtime_error("Stale EntityHandle"); }` — Методы Entity сначала зовут `check()`: ошибка вместо висячего указателя.
5. **стр. 35** `std::weak_ptr<ApiState> state;` — Прокси мира тоже держит слабую ссылку.
6. **стр. 38** `if (!s || !s->active) throw std::runtime_error("Script world is no longer active");` — После Stop — понятная ошибка, а не обращение к удалённому.

### Кто что решает

**C++ — сервисы:**
- **Ввод:** actions `StartWave` = Space, `DefensePulse` = F, только при фокусе окна Game.
- **Поиск:** по тегу, без помеченных на удаление.
- **Спавн и уничтожение:** префаб одной командой, удаление после кадра.
- **Трансформ и анимация:** позиция, поворот, скорость анимации.
- **HUD:** строка статуса в окне Game.
- **Физика и рендер** — целиком C++, скрипты их не трогают.

**Lua — решения:**
- Когда начать волну и сколько в ней врагов.
- Где появится враг и куда он идёт.
- Сколько урона и когда враг исчезает.
- Когда сработает импульс и кого он уничтожит.
- Когда поражение и что написать в HUD.
- Числа баланса — поля, их видят инспектор и префаб.

> **Честно о спорном:** список префабов арены (`loadArenaScene`) и клавиши Space/F заданы в C++. Это описание сцены и ввода, а не правила; вынести в JSON — около часа работы.

### Могут спросить

**Как скрипт ссылается на C++-объекты?**

`EntityHandle {World*, weak_ptr lifetime, id, generation}`. `valid()`: токен мира жив, сущность жива, поколение совпало. Это «opaque handle + уникальный id» из лекции 5: переживает перестройку хеш-таблиц и ловит протухшие ссылки.

**Враг уничтожен, а скрипт хранит его entity. Что будет?**

`is_alive()` вернёт false. `get_position` бросит «Stale EntityHandle» с файл:строка; экземпляр, который ошибся, отключится, движок продолжит. Указателя на компонент у скрипта нет вообще.

**Что со ссылками при Stop?**

`stop()` выключает World API и создаёт новый ApiState — старые `self.world` навсегда «no longer active». Снимок сцены восстанавливается через `world.clear()`: токен мира меняется, и все старые хэндлы протухают, даже если id совпадёт.

**А в обратную сторону — C++ держит Lua-объекты?**

Только `sol::table`/`sol::reference` внутри `ScriptSystem::Impl`. Они объявлены после `sol::state` и разрушаются раньше него; Stop удаляет экземпляры, GC собирает таблицы.

**Почему find_all_by_tag возвращает таблицу?**

`sol::as_table` превращает `std::vector` в обычную Lua-таблицу с индексами с 1 — работают `ipairs` и `#`.

**Что если скрипт передаст NaN в set_position?**

`checkPosition` бросит «Position must be finite» — ошибка с файл:строка, в трансформ мусор не попадёт.

## 7. Кадр скриптов

**Кадр скриптов: обход по снимку, удаление после обхода, ошибка выключает один экземпляр.**

Один кадр арены по шагам: волны спавнят врага, другой враг доходит до ядра, третий падает с ошибкой.

1. **Снимок.** Копия списка сущностей: что заспавнят в этом кадре, в обход не попадёт.
2. **Core · on_update.** Перезарядка идёт, F не нажата — импульса нет.
3. **Waves · on_update.** Таймер вышел: `spawn_prefab` создаёт Enemy #7 — сразу в мире, но не в снимке.
4. **Enemy #5 · дошёл.** `set_field("health", 9)` и `destroy(self)` — только пометка в наборе.
5. **Enemy #6 · ошибка.** Исключение поймано: в лог файл:строка, экземпляр помечен failed и дальше не тикает.
6. **После обхода.** Для #5 — `on_destroy`, затем `destroyEntity`. Итерация уже закончена — ломать нечего.
7. **Хвост кадра.** Шаг GC, время скриптов для окна Gameplay, графики Tracy. В следующем кадре #7 получит экземпляр и `on_create`.

Итог кадра по сущностям:

| Сущность | В снимке | Что произошло |
|---|---|---|
| Core #2 | да | `on_update`: перезарядка −dt |
| Waves #3 | да | `on_update`: `spawn_prefab` → #7 |
| Enemy #5 | да | health 10 → 9, `destroy(self)` → после обхода `on_destroy` и `destroyEntity` |
| Enemy #6 | да | ошибка → failed, больше не тикает |
| Enemy #7 | нет | создан в этом кадре; `on_create` — в следующем |

`src/scripting/ScriptSystem.cpp`, строки 622–650:

```cpp
void ScriptSystem::update(float dt, bool allowInput) {
    auto& p = *impl_; if (!running()) return;
    ZoneScopedN("Lua: update");
    const auto started = std::chrono::steady_clock::now();
    p.api->allowInput = allowInput;
    const auto entities = p.world.getEntities(); // Spawning cannot invalidate this iteration.
    for (Entity e : entities) if (p.world.hasComponent<ScriptComponent>(e) && !p.api->destroyed.count(e)) {
        try {
            auto old = p.instances.find(e);
            if (old != p.instances.end() && !old->second.entity.valid()) p.instances.erase(old);
            if (!p.instances.count(e)) p.create(e);
            auto& i = p.instances.at(e);
            if (!i.failed && i.entity.valid()) i.behaviour->on_update(dt);
        } catch (const std::exception& x) {
            p.report(x);
            if (p.instances.count(e)) p.instances.at(e).failed = true;
            else p.api->destroyed.insert(e);
        }
    }
    auto destroyed = std::move(p.api->destroyed); p.api->destroyed.clear();
    for (Entity e : destroyed) { p.remove(e); p.world.destroyEntity(e); }
    for (auto it = p.instances.begin(); it != p.instances.end();) {
        if (!it->second.entity.valid()) it = p.instances.erase(it); else ++it;
    }
    lua_gc(p.lua.lua_state(), LUA_GCSTEP, 32);
    p.updateMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    TracyPlot("Lua memory KB", static_cast<int64_t>(p.sandbox.used / 1024));
    TracyPlot("Lua instances", static_cast<int64_t>(p.instances.size()));
}
```

1. **стр. 627** `const auto entities = p.world.getEntities(); // Spawning cannot invalidate this iteration.` — Обходим копию списка: спавн во время обхода её не ломает.
2. **стр. 628** `for (Entity e : entities) if (p.world.hasComponent<ScriptComponent>(e) && !p.api->destr…` — Помеченные на удаление в этом кадре не обновляются.
3. **стр. 632** `if (!p.instances.count(e)) p.create(e);` — Новая сущность получает экземпляр и `on_create`.
4. **стр. 634** `if (!i.failed && i.entity.valid()) i.behaviour->on_update(dt);` — Отключённый ошибкой экземпляр пропускается.
5. **стр. 637** `if (p.instances.count(e)) p.instances.at(e).failed = true;` — Ошибка выключает один экземпляр, а не весь Play.
6. **стр. 642** `for (Entity e : destroyed) { p.remove(e); p.world.destroyEntity(e); }` — Удаление после обхода: `on_destroy`, затем `destroyEntity`.
7. **стр. 646** `lua_gc(p.lua.lua_state(), LUA_GCSTEP, 32);` — Шаг инкрементальной сборки мусора каждый кадр.
8. **стр. 648** `TracyPlot("Lua memory KB", static_cast<int64_t>(p.sandbox.used / 1024));` — Графики памяти и экземпляров в Tracy.

### Могут спросить

**Почему destroy отложенный?**

Удаление во время обхода сломало бы итерацию и порядок: следующий скрипт мог бы получить сущность без компонентов. Поэтому `destroy` только помечает, а удаление — после прохода. Это «очередь мутаций» из лекции 5.

**Почему спавн в update не ломает обход?**

Обходим копию `getEntities()`. Новая сущность уже в мире (её видит `find_all_by_tag`), но экземпляр и `on_create` получит в следующем кадре.

**Есть ли у вас one-frame-off lag?**

Да, в мягкой форме: скрипты идут по порядку id, враг, обновлённый раньше, уже снял HP ядра — следующие видят новое значение в этом же кадре. Позиции после физики — с прошлого кадра. Решения от порядка врагов не зависят, поэтому ошибок это не даёт.

**Модель обновления у вас batched или phased?**

Batched: одна система обходит все скрипты разом. Phased: скрипты — отдельная фаза кадра, до физики и анимации.

**Что значит «ошибка выключает один экземпляр»?**

Исключение из `on_update` ловится в цикле: экземпляр помечается `failed` и больше не вызывается. Остальные работают, Play продолжается. Ожить он может после Play или после hot reload исправленного файла.

**Когда скрипт видит ввод?**

`allowInput` = окно Game в фокусе и не идёт ввод текста. Без клика по Game Space и F до скриптов не доходят — главная ловушка демо.

## 8. Префабы и поля

**Префаб — это данные: компоненты и поля поведения в JSON; Lua объявляет тип, JSON и инспектор — значение.**

### Префаб → сущность

`assets/prefabs/enemy.json`, строки 1–10:

```json
{
  "components": {
    "tag": "Enemy",
    "transform": {"rotation": [1.570796327, 0, 0], "scale": [0.01106, 0.01106, 0.01106]},
    "meshRenderer": {"mesh": "assets/models/animation/Walking.fbx"},
    "animator": {"speed": 1.0, "inPlaceNode": "mixamorig:Hips"},
    "script": {"path": "assets/scripts/enemy.lua", "class": "Enemy", "fields": {"speed": 1.5, "damage": 1, "attack_radius": 1.2, "animation_speed": 1.0, "target_tag": "Core"}}
  }
}

```

1. **стр. 3** `"tag": "Enemy",` — Тег — по нему скрипты находят сущность (`find_all_by_tag("Enemy")`).
2. **стр. 5** `"meshRenderer": {"mesh": "assets/models/animation/Walking.fbx"},` — Модель с анимацией: обычный компонент, не скрипт.
3. **стр. 6** `"animator": {"speed": 1.0, "inPlaceNode": "mixamorig:Hips"},` — Скорость анимации потом меняет скрипт.
4. **стр. 7** `"script": {"path": "assets/scripts/enemy.lua", "class": "Enemy", "fields": {"speed": 1.…` — Файл и класс поведения + значения полей. Они переопределяют значения по умолчанию из Lua.

`src/prefabs/PrefabManager.cpp`, строки 39–58, 89–91:

```cpp
Entity PrefabManager::spawn(World& world,const std::string& path,bool renderResources){
    std::ifstream in(path);if(!in)throw std::runtime_error("Cannot open prefab: "+path);
    Json document;in>>document;
    const auto& c=document.at("components");
    const auto t=c.value("transform",Json::object());
    Transform transform{vector(t.value("position",Json()),{}),vector(t.value("rotation",Json()),{}),vector(t.value("scale",Json()),{1,1,1})};
    ScriptComponent script;
    if(c.contains("script")) {
        const auto& s=c.at("script");
        script.path=s.at("path").get<std::string>();script.className=s.at("class").get<std::string>();script.prefab=path;
        const auto fields=s.value("fields",Json::object());
        if(!fields.is_object())throw std::runtime_error("Prefab fields must be an object");
        for(const auto& [name,v]:fields.items())script.fields[name]=value(v);
    }
    Entity e=world.createEntity();
    try {
        world.addComponent<Tag>(e,Tag{c.value("tag",std::filesystem::path(path).stem().string())});
        world.addComponent<Transform>(e,transform);
        if(c.contains("script"))world.addComponent<ScriptComponent>(e,script);
        if(c.contains("animator")) {
// ⋯
    } catch(...) {world.destroyEntity(e);throw;}
    return e;
}
```

1. **стр. 48** `script.path=s.at("path").get<std::string>();script.className=s.at("class").get<std::str…` — Компонент помнит свой префаб — Save to Prefab пишет именно туда.
2. **стр. 51** `for(const auto& [name,v]:fields.items())script.fields[name]=value(v);` — Поля из JSON: bool, int, number, string; 2 и 2.0 различаются.
3. **стр. 53** `Entity e=world.createEntity();` — Сущность создаётся только после разбора JSON.
4. **стр. 89** `} catch(...) {world.destroyEntity(e);throw;}` — Ошибка посередине — откат: сущность удаляется, исключение идёт дальше.

### Три слоя значения поля

1. **Lua: `fields`** — тип и значение по умолчанию (как type schema).
2. **Префаб JSON** — переопределение (как spawner).
3. **Инспектор или скрипт** — правка в Edit или `set_field` в Play.

Итог хранится в `ScriptComponent::fields` (`variant<bool, int, double, string>`).

| Случай | Lua | JSON | Инспектор / скрипт | Итог | Лог |
|---|---|---|---|---|---|
| Как в репозитории | `speed = 1.5` | `"speed": 1.5` | — | `speed = 1.5` (number) | — |
| 2 вместо 2.0 | `speed = 1.0` | `"speed": 2` | — | `speed = 2.0` (number) | coerce: целое расширено до number, без предупреждения |
| Правка в инспекторе | `damage = 1` | `"damage": 1` | Damage → 2 | `damage = 2` до Stop; Save to Prefab — в JSON | следующий Play видит 2; новые враги — только после Save |
| Дробное в целое | `health = 10` | `"health": 10` | `set_field("health", 8.5)` | `health = 10` (не изменилось) | `core.lua:N: Core: field 'health' expects integer, got number` |
| 4.0 в целое | `health = 10` | `"health": 10` | `set_field("health", 4.0)` | `health = 4` (integer) | дробной части нет — сужение до целого разрешено |
| Поле удалили из Lua | — (`legacy` нет) | `"legacy": 1` | — | `legacy` выброшено | `[WARN] Lua: assets/prefabs/enemy.json: field 'legacy' is not declared by Enemy; dropped` |
| Сменился тип | `flag = true` | `"flag": "yes"` | — | `flag = true` (boolean) | `[WARN] Lua: …: field 'flag' is string, class … declares boolean; reset to default` |
| Новое поле в Lua | `boost = 2` | — | — | `boost = 2` (integer, по умолчанию) | появляется и в Edit, и прямо в Play после hot reload |

`src/scripting/ScriptSystem.cpp`, строки 72–86, 417–437:

```cpp
bool coerce(ScriptValue& value, const ScriptValue& like) {
    if (value.index() == like.index()) return true;
    if (std::holds_alternative<double>(like) && std::holds_alternative<int>(value)) {
        value = static_cast<double>(std::get<int>(value));
        return true;
    }
    if (std::holds_alternative<int>(like) && std::holds_alternative<double>(value)) {
        const double number = std::get<double>(value);
        if (std::trunc(number) == number && number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max()) {
            value = static_cast<int>(number);
            return true;
        }
    }
    return false;
}
// ⋯
    void defaults(ScriptComponent& c, const sol::table& cls) {
        sol::table defaults = cls["fields"];
        const std::string owner = c.prefab.empty() ? c.path : c.prefab;
        for (const auto& item : defaults) {
            const auto name = item.first.as<std::string>();
            const auto value = fromLua(item.second);
            auto it = c.fields.find(name);
            if (it == c.fields.end()) {
                c.fields[name] = value;
            } else if (!coerce(it->second, value)) {
                LOG_WARN("Lua: " + owner + ": field '" + name + "' is " + typeName(it->second) + ", class " + c.className +
                    " declares " + typeName(value) + "; reset to default");
                it->second = value;
            }
        }
        for (auto it = c.fields.begin(); it != c.fields.end();) {
            if (defaults.raw_get<sol::object>(it->first) != sol::lua_nil) { ++it; continue; }
            LOG_WARN("Lua: " + owner + ": field '" + it->first + "' is not declared by " + c.className + "; dropped");
            it = c.fields.erase(it);
        }
    }
```

1. **стр. 74** `if (std::holds_alternative<double>(like) && std::holds_alternative<int>(value)) {` — Целое расширяется до number: `"speed": 2` при `speed = 1.0` — нормально.
2. **стр. 80** `if (std::trunc(number) == number && number >= std::numeric_limits<int>::min() && number…` — Number сужается до целого только без дробной части: 4.0 → 4, 4.5 — ошибка.
3. **стр. 425** `c.fields[name] = value;` — Новое поле получает значение по умолчанию из Lua.
4. **стр. 428** `" declares " + typeName(value) + "; reset to default");` — Тип сменился — WARN и значение по умолчанию; reload не падает.
5. **стр. 434** `LOG_WARN("Lua: " + owner + ": field '" + it->first + "' is not declared by " + c.classN…` — Поле удалили из Lua — WARN и поле выбрасывается.

### Inspector и Save to Prefab

1. **Выделили объект.** Карточка Script: файл, префаб и все поля класса — по алфавиту, виджет по типу: чекбокс, целое, число, строка.
2. **Правим в Edit.** Пишем прямо в `ScriptComponent`. Скрипт читает поле через `get_field` при каждом вызове — следующий Play увидит новое значение.
3. **Play.** Поля только для чтения, но живые: видно, как падает Health ядра. Stop возвращает значения до Play.
4. **Save to Prefab.** Читаем JSON, проверяем, что путь и класс скрипта не поменялись, заменяем `fields` с сохранением порядка ключей (`ordered_json`), пишем через `.tmp` и rename.
5. **Следующий спавн.** Враги создаются из JSON на диске, поэтому правка превью врага без Save в новых врагах не появится.

> **Почему поля — в компоненте, а не в Lua?** `ScriptComponent` — чистые данные: путь, класс, префаб, значения. Его копирует снимок Play, дублирует редактор, пишет JSON — и всё это не зависит от жизни VM.
>
> **Reset** в карточке Script сбрасывает поля к значениям из Lua и работает только в Edit: в Play поля живые.

### Могут спросить

**Что такое префаб у вас и чем он похож на spawner из лекции?**

JSON с компонентами и начальными значениями — это spawner: лёгкое data-only описание объекта. Объявление `fields` в Lua — type schema: имена, типы, значения по умолчанию, по типу выбирается виджет инспектора. Атрибуты по умолчанию в JSON можно не писать.

**Что если спавн упадёт посередине?**

Сущность создаётся только после разбора JSON; если компонент не добавился — `catch(...)` удаляет сущность и пробрасывает исключение дальше. Полусобранных объектов в мире не остаётся.

**Чем 2 отличается от 2.0 и почему это важно?**

Lua 5.4 различает integer и float, JSON — тоже. Тип поля задаёт значение по умолчанию в Lua. Целое в number-поле расширяем молча; дробное в целое поле — только если без дробной части (4.0 → 4), иначе ошибка «expects integer».

**Что будет, если удалить поле из Lua, а в префабе оно осталось?**

Поле выбрасывается из компонента с WARN в логе, reload не падает. После Save to Prefab оно исчезнет и из JSON.

**Как Save to Prefab не портит файл?**

Пишем во временный `.tmp` и заменяем rename-ом (на Windows — `MoveFileExW`): при сбое старый файл цел. `ordered_json` сохраняет порядок ключей, чтобы `git diff` показывал только изменённые числа.

## 9. Механика на Lua

**Механика целиком в трёх файлах: волны, враг и ядро; C++ только исполняет.**

Числа баланса — поля, правила — код:
- волна N = 4 + 2·(N − 1) врагов;
- спавн каждые 0,6 с на радиусе 10 вокруг ядра, через равные углы;
- враг идёт со скоростью 1,5, у радиуса 1,2 снимает 1 HP и исчезает;
- импульс по F уничтожает врагов в радиусе 6, перезарядка 2 с;
- у ядра 10 HP; HP = 0 → DEFEAT;
- Space запускает волну, только когда прошлая уничтожена.

`assets/scripts/waves.lua`, строки 8–55:

```lua
Waves = uzlezz.behaviour {
    fields = {first_wave_count = 4, count_increment = 2,
              spawn_interval = 0.6, spawn_radius = 10.0,
              enemy_prefab = "assets/prefabs/enemy.json"}
}

function Waves:on_create()
    assert(self.entity:get_field("first_wave_count") >= 1 and self.entity:get_field("count_increment") >= 0,
           "Wave count must be positive and increment nonnegative")
    assert(self.entity:get_field("spawn_interval") > 0 and self.entity:get_field("spawn_radius") > 1.2,
           "Spawn interval must be positive and radius > 1.2")
    self.wave, self.remaining, self.timer, self.serial = 0, 0, 0.0, 0
    self.wave_count = 0
end

function Waves:on_update(dt)
    local core = self.world:find_by_tag("Core")
    assert(core:is_alive(), "Wave scene needs a Core")
    local health = core:get_field("health")
    if health <= 0 then
        self.world:set_status(string.format("DEFEAT. Wave %d. Stop / Play to restart.", self.wave))
        return
    end
    local enemies = #self.world:find_all_by_tag("Enemy")
    if self.remaining == 0 and enemies == 0 and self.world:input_pressed("StartWave") then
        self.wave = self.wave + 1
        self.remaining = self.entity:get_field("first_wave_count") + (self.wave - 1) * self.entity:get_field("count_increment")
        self.wave_count = self.remaining
        self.serial = 0
        self.timer = 0.0
    end
    self.timer = self.timer - dt
    if self.remaining > 0 and self.timer <= 0.0 then
        -- Evenly spaced approaches, clockwise around the arena, reset for each wave.
        local angle = -math.pi / 2 + self.serial * (2 * math.pi / self.wave_count)
        local center = core:get_position()
        local radius = self.entity:get_field("spawn_radius")
        self.world:spawn_prefab(self.entity:get_field("enemy_prefab"),
            uzlezz.Vec3(center.x + math.cos(angle) * radius, center.y + math.sin(angle) * radius, 0.0))
        self.serial = self.serial + 1
        self.remaining = self.remaining - 1
        self.timer = self.entity:get_field("spawn_interval")
        enemies = enemies + 1
    end
    local hint = enemies == 0 and self.remaining == 0 and "Space: next wave" or "F: defense pulse"
    self.world:set_status(string.format("Core HP: %d | Wave: %d | Alive: %d | Pending: %d | %s",
        health, self.wave, enemies, self.remaining, hint))
end
```

1. **стр. 9** `fields = {first_wave_count = 4, count_increment = 2,` — Числа баланса — поля: значения по умолчанию, JSON их переопределяет.
2. **стр. 19** `self.wave, self.remaining, self.timer, self.serial = 0, 0, 0.0, 0` — Состояние механики живёт в `self` экземпляра.
3. **стр. 32** `if self.remaining == 0 and enemies == 0 and self.world:input_pressed("StartWave") then` — Правило: новая волна только когда прошлая уничтожена и нажат Space.
4. **стр. 34** `self.remaining = self.entity:get_field("first_wave_count") + (self.wave - 1) * self.ent…` — Размер волны: 4, 6, 8…
5. **стр. 42** `local angle = -math.pi / 2 + self.serial * (2 * math.pi / self.wave_count)` — Порядок: враги заходят по кругу через равные углы.
6. **стр. 45** `self.world:spawn_prefab(self.entity:get_field("enemy_prefab"),` — Спавн из шаблона одной командой.
7. **стр. 53** `self.world:set_status(string.format("Core HP: %d | Wave: %d | Alive: %d | Pending: %d |…` — Строка HUD: её рисует окно Game.

`assets/scripts/enemy.lua`, строки 3–31:

```lua
Enemy = uzlezz.behaviour {
    fields = {speed = 1.5, damage = 1, attack_radius = 1.2,
              animation_speed = 1.0, target_tag = "Core"}
}

function Enemy:on_create()
    assert(self.entity:get_field("speed") > 0 and self.entity:get_field("damage") > 0,
           "Enemy speed and damage must be positive")
    self.entity:set_animation_speed(self.entity:get_field("animation_speed"))
end

function Enemy:on_update(dt)
    local core = self.world:find_by_tag(self.entity:get_field("target_tag"))
    if not core:is_alive() or core:get_field("health") <= 0 then
        self.entity:set_animation_speed(0.0)
        return
    end
    local p, target = self.entity:get_position(), core:get_position()
    local dx, dy = target.x - p.x, target.y - p.y
    local distance = math.sqrt(dx * dx + dy * dy)
    if distance <= math.max(0.1, self.entity:get_field("attack_radius")) then
        core:set_field("health", math.max(0, core:get_field("health") - self.entity:get_field("damage")))
        self.world:destroy(self.entity)
        return
    end
    local step = math.min(distance, self.entity:get_field("speed") * dt)
    self.entity:set_position(uzlezz.Vec3(p.x + dx / distance * step, p.y + dy / distance * step, p.z))
    self.entity:set_yaw(math.atan(dx, -dy))
end
```

1. **стр. 15** `local core = self.world:find_by_tag(self.entity:get_field("target_tag"))` — Цель ищется по тегу из поля — тоже данные.
2. **стр. 16** `if not core:is_alive() or core:get_field("health") <= 0 then` — Ядро мертво — враг замирает.
3. **стр. 23** `if distance <= math.max(0.1, self.entity:get_field("attack_radius")) then` — Дошёл до радиуса атаки…
4. **стр. 24** `core:set_field("health", math.max(0, core:get_field("health") - self.entity:get_field("…` — …снимает HP ядра (целое поле!)…
5. **стр. 25** `self.world:destroy(self.entity)` — …и уничтожает себя (отложенно).
6. **стр. 28** `local step = math.min(distance, self.entity:get_field("speed") * dt)` — Иначе шаг к цели со скоростью `speed`.

`assets/scripts/core.lua`, строки 4–27, 29:

```lua
Core = uzlezz.behaviour {
    fields = {health = 10, pulse_radius = 6.0, pulse_cooldown = 2.0}
}

function Core:on_create()
    self.cooldown = 0.0
    assert(self.entity:get_field("health") > 0, "Core health must be positive")
end

function Core:on_update(dt)
    self.cooldown = math.max(0.0, self.cooldown - dt)
    if self.entity:get_field("health") <= 0 then return end
    if self.world:input_pressed("DefensePulse") and self.cooldown == 0.0 then
        local center = self.entity:get_position()
        local radius = math.max(0.0, self.entity:get_field("pulse_radius"))
        for _, enemy in ipairs(self.world:find_all_by_tag("Enemy")) do
            local p = enemy:get_position()
            local dx, dy = p.x - center.x, p.y - center.y
            if dx * dx + dy * dy <= radius * radius then
                self.world:destroy(enemy)
            end
        end
        self.cooldown = math.max(0.0, self.entity:get_field("pulse_cooldown"))
    end
-- ⋯
end
```

1. **стр. 9** `self.cooldown = 0.0` — Перезарядка — состояние экземпляра.
2. **стр. 16** `if self.world:input_pressed("DefensePulse") and self.cooldown == 0.0 then` — F и перезарядка прошла…
3. **стр. 19** `for _, enemy in ipairs(self.world:find_all_by_tag("Enemy")) do` — …обходим всех врагов…
4. **стр. 22** `if dx * dx + dy * dy <= radius * radius then` — …и уничтожаем тех, кто внутри радиуса.

### Могут спросить

**Где тут «реакция на события»?**

Скрипты реагируют на ввод (Space, F) и сами проверяют мир каждый кадр: жив ли ядро, дошёл ли враг. Шина событий в движке есть (`EventDispatcher`, синхронная доставка — её публикует физика), но подписки из Lua мы не делали: это допфича «события движка в скрипте».

**Почему враг сам себя уничтожает, а не ядро его?**

Решение «дошёл — ударил — исчез» принимает враг; ядро только хранит HP. Так правило урона меняется в одном месте — `enemy.lua`.

**Когда поражение без импульсов?**

В конце второй волны: 4 + 6 = 10 урона при 10 HP. Враг доходит примерно за 6 секунд: (10 − 1,2) / 1,5.

**Почему damage целое, а speed дробное?**

Тип задаёт значение по умолчанию: `damage = 1` — integer, `speed = 1.5` — number. HP ядра целое; дробный урон дал бы «field 'health' expects integer».

## 10. Hot reload через job system

**Сохранили `.lua` — воркер нашёл и проверил файл, главный поток подменил код между кадрами.**

Сквозное правило семестра из ЛР 1: тяжёлое — задачей в пуле, итог — на главном потоке.

Конвейер:
1. **Главный поток, раз в 0,2 с:** если прошлая задача закончилась — `JobSystem::submitBackground(scan, Low)`.
2. **Воркер (`ScriptWatcher::scan`):** обходит `assets/scripts` и файлы сцены; метка = `last_write_time` + размер; ждёт, пока метка не изменится два опроса подряд; читает файл; компилирует в одноразовом `luaL_newstate()` через `luaL_loadbufferx(src, "@path", "t")` — только компиляция, без выполнения; кладёт `{path, source, ok, error}` в очередь под mutex.
3. **Главный поток:** `drain()` → `EditorContext::applyScriptChange` → `ScriptSystem::hotReload(path, source, mode)`.

Работает в Edit и в Play. Кнопка Reload Scripts делает то же для всех файлов сцены.

### Сценарии по шагам

**Обычное сохранение**

| Время | Кто | Событие |
|---|---|---|
| 0,00 с | главный поток | poll: прошло 0,2 с, прошлая задача закончилась — `submitBackground(scan, Low)` |
| 0,01 с | воркер | scan: `enemy.lua` уже известен, метка прежняя — изменений нет |
| 0,07 с | редактор кода | ⌘S: записан `enemy.lua` (правка А: скорость ×3) |
| 0,20 с | главный поток | poll — новая задача |
| 0,21 с | воркер | pending: метка изменилась впервые, файл, возможно, ещё пишется — ждём |
| 0,40 с | главный поток | poll |
| 0,41 с | воркер | validate ✓: метка совпала второй раз → читаем и компилируем; синтаксис верен; изменение — в очередь ready |
| 0,43 с | главный поток | hotReload: класс проверен и подменён, 6 врагов на новом коде, состояние сохранено (L2); в лог «enemy.lua reloaded in Play» |

**Запись по частям** — редактор пишет файл в два приёма (0,07 с и 0,27 с). На 0,21 с метка изменилась → pending; на 0,41 с метка не совпала с pending → ждём ещё; на 0,61 с метка стабильна → validate ✓ → применение на 0,63 с. Без debounce на 0,21 с проверялась бы половина файла и вышла бы ложная синтаксическая ошибка.

**Синтаксическая ошибка** — сохранили `enemy.lua` без закрывающего `end` (0,07 с) → pending (0,21 с) → validate ✗ (0,41 с): `assets/scripts/enemy.lua:31: 'end' expected (to close 'function' at line 14) near <eof>` → главный поток (0,43 с): ошибка в лог и окно Gameplay — «Not reloaded, the previous code stays active», враги идут на старом коде. Вернули `end` (0,66 с) → pending (0,81 с) → validate ✓ (1,01 с) → новый код применён (1,03 с).

**Файл не нужен сцене** — сохранили `notes.lua`, его не использует ни одна сущность. Вотчер смотрит всю папку и проверяет файл, но в `hotReload` список классов этого файла пуст — ничего не делаем и ничего не пишем.

`src/scripting/ScriptWatcher.cpp`, строки 37–96:

```cpp
void ScriptWatcher::scan(Shared& shared, const std::vector<std::string>& roots, const std::vector<std::string>& paths) {
    ZoneScopedN("Lua watcher: scan");
    std::set<std::string> files;
    for (const std::string& path : paths) {
        files.insert(normalize(path));
    }
    std::error_code code;
    for (const std::string& root : roots) {
        for (std::filesystem::recursive_directory_iterator it(root, code), end; !code && it != end; it.increment(code)) {
            if (it->is_regular_file(code) && it->path().extension() == ".lua") {
                files.insert(normalize(it->path()));
            }
        }
        code.clear();
    }

    for (const std::string& path : files) {
        Stamp stamp;
        stamp.time = std::filesystem::last_write_time(path, code);
        if (code) { code.clear(); continue; }
        stamp.size = std::filesystem::file_size(path, code);
        if (code) { code.clear(); continue; }
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            auto known = shared.known.find(path);
            if (known == shared.known.end()) {
                shared.known.emplace(path, stamp); // первая встреча — базовая версия, не изменение
                continue;
            }
            if (known->second == stamp) {
                shared.pending.erase(path);
                continue;
            }
            auto pending = shared.pending.find(path);
            if (pending == shared.pending.end() || pending->second != stamp) {
                shared.pending[path] = stamp; // ждём, пока запись файла закончится
                continue;
            }
        }
        // Метка стабильна два опроса: читаем и проверяем без блокировки.
        Change change;
        change.path = path;
        {
            ZoneScopedN("Lua watcher: validate");
            std::ifstream in(path, std::ios::binary);
            if (in) {
                change.source.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                change.ok = validate(path, change.source, change.error);
            } else {
                change.error = path + ": cannot read file";
            }
        }
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.known[path] = stamp;
        shared.pending.erase(path);
        shared.ready.push_back(std::move(change));
    }
    std::lock_guard<std::mutex> lock(shared.mutex);
    ++shared.scans;
}
```

1. **стр. 38** `ZoneScopedN("Lua watcher: scan");` — Работает на воркере job system — зона видна на его дорожке в Tracy.
2. **стр. 45** `for (std::filesystem::recursive_directory_iterator it(root, code), end; !code && it !=…` — Все .lua в `assets/scripts` + файлы, на которые ссылается сцена.
3. **стр. 55** `stamp.time = std::filesystem::last_write_time(path, code);` — Метка = время записи (точность — наносекунды) + размер.
4. **стр. 63** `shared.known.emplace(path, stamp); // первая встреча — базовая версия, не изменение` — Первая встреча — базовая версия, не изменение.
5. **стр. 72** `shared.pending[path] = stamp; // ждём, пока запись файла закончится` — Изменилось впервые — ждём следующего опроса (debounce).
6. **стр. 84** `change.ok = validate(path, change.source, change.error);` — Метка стабильна: читаем и компилируем — без блокировки.
7. **стр. 92** `shared.ready.push_back(std::move(change));` — Готовое изменение — в очередь для главного потока.

`src/scripting/ScriptWatcher.cpp`, строки 21–35, 98–110:

```cpp
bool ScriptWatcher::validate(const std::string& path, const std::string& source, std::string& error) {
    lua_State* L = luaL_newstate();
    if (!L) {
        error = path + ": cannot create a Lua state for validation";
        return false;
    }
    const std::string chunk = "@" + path; // как у luaL_loadfile: ошибки вида "path:line: ..."
    const bool ok = luaL_loadbufferx(L, source.data(), source.size(), chunk.c_str(), "t") == LUA_OK;
    if (!ok) {
        const char* message = lua_tostring(L, -1);
        error = message ? message : path + ": unknown syntax error";
    }
    lua_close(L);
    return ok;
}
// ⋯
void ScriptWatcher::poll(double nowSeconds, const std::vector<std::string>& roots, const std::vector<std::string>& paths) {
    if (!due(nowSeconds)) {
        return;
    }
    lastPoll_ = nowSeconds;
    JobSystem& jobs = JobSystem::getInstance();
    if (!jobs.isRunning()) {
        scanNow(roots, paths);
        return;
    }
    // Фоновая задача: главный поток её не крадёт, кадр не ждёт диска и компиляции.
    job_ = jobs.submitBackground([shared = shared_, roots, paths] { scan(*shared, roots, paths); }, JobPriority::Low);
}
```

1. **стр. 22** `lua_State* L = luaL_newstate();` — Одноразовый state только для проверки — общий state воркер не трогает.
2. **стр. 28** `const bool ok = luaL_loadbufferx(L, source.data(), source.size(), chunk.c_str(), "t") =…` — Только компиляция, без выполнения; `@path` даёт ошибки вида «path:line:».
3. **стр. 99** `if (!due(nowSeconds)) {` — Раз в 0,2 с и только если прошлая задача закончилась.
4. **стр. 109** `job_ = jobs.submitBackground([shared = shared_, roots, paths] { scan(*shared, roots, pa…` — Фоновая задача Low: главный поток её не крадёт, кадр не ждёт.

`src/editor/EditorContext.cpp`, строки 194–203, 359–370:

```cpp
    // Шейдеры перезагружает Application (HotReload); здесь — скрипты.
    const double now = ImGui::GetTime();
    if (autoReloadScripts && scriptWatcher.due(now)) {
        scriptWatcher.poll(now, {kScriptRoot}, scripts.scriptPaths());
    }
    for (const ScriptWatcher::Change& change : scriptWatcher.drain()) {
        if (autoReloadScripts) {
            applyScriptChange(change);
        }
    }
// ⋯
bool EditorContext::applyScriptChange(const ScriptWatcher::Change& change) {
    const std::string name = std::filesystem::path(change.path).filename().string();
    if (!change.ok) {
        LOG_ERROR("Lua: " + change.error);
        setScriptMessage("Not reloaded, the previous code stays active. " + change.error, true);
        return false;
    }
    const HotReloadReport report = scripts.hotReload(change.path, change.source, playReloadMode);
    if (!report.ok) {
        setScriptMessage("Not reloaded, the previous code stays active. " + report.error, true);
        return false;
    }
```

1. **стр. 197** `scriptWatcher.poll(now, {kScriptRoot}, scripts.scriptPaths());` — Каждый кадр: поставить задачу, если пора.
2. **стр. 199** `for (const ScriptWatcher::Change& change : scriptWatcher.drain()) {` — Забрать готовые изменения — на главном потоке.
3. **стр. 361** `if (!change.ok) {` — Синтаксическая ошибка: в лог и в окно Gameplay, старый код работает.
4. **стр. 366** `const HotReloadReport report = scripts.hotReload(change.path, change.source, playReload…` — Применение: Edit или Play, режим L1/L2 из окна Gameplay.

`src/scripting/ScriptSystem.cpp`, строки 534–604:

```cpp
HotReloadReport ScriptSystem::hotReload(const std::string& path, const std::string& source, ReloadMode mode) {
    ZoneScopedN("Lua: hot reload");
    auto& p = *impl_;
    HotReloadReport report;
    const std::string file = normalizePath(path);
    // Классы этого файла: уже загруженные и те, что объявлены компонентами сцены. Ключ — как в classes.
    std::map<std::string, std::pair<std::string, std::string>> targets;
    for (const auto& item : p.classes) {
        const auto colon = item.first.rfind(':');
        const std::string classPath = item.first.substr(0, colon);
        if (normalizePath(classPath) == file) targets[item.first] = {classPath, item.first.substr(colon + 1)};
    }
    for (Entity e : p.world.getEntities()) {
        if (!p.world.hasComponent<ScriptComponent>(e)) continue;
        const auto& c = p.world.getComponent<ScriptComponent>(e);
        if (normalizePath(c.path) == file) targets[p.key(c)] = {c.path, c.className};
    }
    if (targets.empty()) { report.ok = true; return report; } // файл сцене не нужен
    // Сначала всё новое собирается и проверяется; при ошибке старые классы и экземпляры остаются.
    std::map<std::string, sol::table> replacement;
    try {
        const sol::environment environment = p.run(file, &source);
        for (const auto& [key, target] : targets) replacement[key] = p.extract(environment, target.first, target.second);
    } catch (const std::exception& x) {
        p.report(x);
        report.error = p.error;
        return report;
    }
    for (auto& [key, cls] : replacement) p.classes[key] = cls;
    report.classes = replacement.size();
    for (Entity e : p.world.getEntities()) {
        if (!p.world.hasComponent<ScriptComponent>(e)) continue;
        auto& c = p.world.getComponent<ScriptComponent>(e);
        if (normalizePath(c.path) == file) p.defaults(c, p.classes.at(p.key(c)));
    }
    if (running()) {
        for (auto& [e, instance] : p.instances) {
            if (!instance.entity.valid() || p.api->destroyed.count(e) || !p.world.hasComponent<ScriptComponent>(e)) continue;
            const auto& c = p.world.getComponent<ScriptComponent>(e);
            if (normalizePath(c.path) != file) continue;
            const sol::table& cls = p.classes.at(p.key(c));
            sol::table object = p.makeObject(cls, e);
            auto behaviour = std::make_unique<LuaBehaviour>(object, cls, p.errorHandler, p.sandbox, c.className);
            try {
                behaviour->on_create();
                const sol::table initial = p.snapshot(object);
                if (mode == ReloadMode::KeepState) {
                    if (behaviour->has("on_reload")) {
                        behaviour->on_reload(instance.behaviour->object());
                        ++report.migrated;
                    } else {
                        report.keptFields += p.transfer(instance.behaviour->object(), instance.initial, object, initial);
                    }
                }
                report.revived += instance.failed ? 1 : 0;
                instance.behaviour = std::move(behaviour);
                instance.initial = initial;
                instance.failed = false;
                ++report.instances;
            } catch (const std::exception& x) {
                p.report(x); // экземпляр остаётся на старом классе
                ++report.failedInstances;
            }
        }
    }
    report.ok = report.failedInstances == 0;
    if (report.ok) p.error.clear(); else report.error = p.error;
    p.lua.collect_garbage();
    return report;
}
bool ScriptSystem::start() {
```

1. **стр. 540** `std::map<std::string, std::pair<std::string, std::string>> targets;` — Какие классы живут в этом файле: загруженные и объявленные сценой.
2. **стр. 551** `if (targets.empty()) { report.ok = true; return report; } // файл сцене не нужен` — Файл сцене не нужен — ничего не делаем.
3. **стр. 555** `const sol::environment environment = p.run(file, &source);` — Новый текст выполняется в новом environment…
4. **стр. 556** `for (const auto& [key, target] : targets) replacement[key] = p.extract(environment, tar…` — …и все классы проверяются. Ошибка — выходим, старые классы на месте.
5. **стр. 562** `for (auto& [key, cls] : replacement) p.classes[key] = cls;` — Всё хорошо — подменяем классы разом.
6. **стр. 567** `if (normalizePath(c.path) == file) p.defaults(c, p.classes.at(p.key(c)));` — Поля компонентов — к новому объявлению.
7. **стр. 575** `sol::table object = p.makeObject(cls, e);` — В Play: новый объект на новом классе…
8. **стр. 578** `behaviour->on_create();` — …его `on_create`…
9. **стр. 582** `behaviour->on_reload(instance.behaviour->object());` — …ручная миграция, если класс её объявил…
10. **стр. 585** `report.keptFields += p.transfer(instance.behaviour->object(), instance.initial, object,…` — …или автоматический перенос L2.
11. **стр. 594** `p.report(x); // экземпляр остаётся на старом классе` — Новый `on_create` упал — экземпляр остаётся на старом коде.

### Могут спросить

**Почему проверка на воркере, а применение на главном потоке?**

Обход папки, чтение и компиляция — работа «к тому моменту, как понадобится», её выносим с главного потока, чтобы кадр не ждал диска. Применение трогает общий `lua_State` и ECS, а они не потокобезопасны, — только главный поток.

**Как воркер проверяет синтаксис, не трогая общий state?**

Создаёт одноразовый `luaL_newstate()`, вызывает `luaL_loadbufferx(src, "@path", "t")` — только компиляция, без выполнения — и закрывает state. Ошибка получается уже в формате «path:line: …».

**Зачем ждать два опроса (debounce)?**

Редактор может писать файл по частям: проверка на половине файла дала бы ложную синтаксическую ошибку. Изменение отдаём, только когда метка (время записи + размер) совпала на двух опросах подряд.

**Почему не взяли HotReload, который был для шейдеров?**

Он сравнивает `time_t` с точностью до секунды — пропустит два сохранения за секунду — и держит одну общую очередь изменений, которую забирает первый потребитель. И работает на главном потоке.

**Что значит «транзакционный» reload?**

Новый текст выполняется в новом environment, все классы файла извлекаются и проверяются. Только если всё прошло, они разом заменяют старые. Любая ошибка — старые классы и экземпляры работают дальше.

**А явной командой можно?**

Да: Reload Scripts в окне Gameplay. В Edit — полный `reload()`, в Play — тот же `hotReload` для каждого файла сцены, прочитанного с диска.

**Что если вотчер ещё работает, когда закрывают редактор?**

Деструктор `ScriptWatcher` ждёт задачу через `JobSystem::wait`. К тому же задача держит общие данные через `shared_ptr` — висячих ссылок нет.

## 11. Reload в Play: L1 и L2

**В Play каждый экземпляр пересоздаётся; L2 переносит состояние — кроме того, что поменяли в коде.**

В обоих режимах каждый живой экземпляр получает новый объект на новом классе и свой `on_create`. Дальше:
- **L1 (Reset):** остаётся состояние из нового `on_create`.
- **L2 (Keep state):** переносятся `self.*` (кроме `entity`/`world`), если ключ есть в новом объекте с тем же типом и его начальное значение в коде не поменялось. Таблицы и хэндлы переносятся по совпадению типа.
- **`on_reload(old)`:** если класс его объявил, перенос делает он сам, автоматический не выполняется.

### Пример: волны в разгаре

Третья волна, двое ждут спавна. Меняем `waves.lua` и сохраняем.

| Поле `self` | было в игре | начальное: старый код | начальное: новый код | после reload |
|---|---|---|---|---|
| `wave` | 3 | 0 | 0 | L2: **3** — перенесено; L1: 0 |
| `remaining` | 2 | 0 | 0 | L2: **2** — перенесено; L1: 0 |
| `timer` | 0,31 | 0 | 0 | L2: **0,31** — перенесено; L1: 0 |
| `serial` | 6 | 0 | 0 | L2: **6** — перенесено; L1: 0 |
| `wave_count` | 8 | 0 | 0 | L2: **8** — перенесено; L1: 0 |

Как меняется итог при правках кода (режим L2):

| Правка в `waves.lua` | Что будет с полем |
|---|---|
| в `on_create`: `self.timer = 1.0` вместо `0.0` | `timer` = **1,0** — начальное значение поменяли в коде, берём новое |
| новое поле `self.bonus = 5` | `bonus` = **5** — нового поля не было, значение из `on_create` |
| `self.serial` переименовали в `self.spawned` | `serial` выброшено (ключа нет в новом объекте), `spawned` = 0 из `on_create` |
| класс объявил `function Waves:on_reload(old) self.wave = old.wave; self.remaining = old.remaining end` | переносятся только `wave` и `remaining`, остальное — из `on_create` |

В лог при L2: «waves.lua reloaded in Play: 1 instance(s), state kept (L2): 5 field(s)». В L1 номер волны начнётся заново — экземпляр как после `on_create`.

`src/scripting/ScriptSystem.cpp`, строки 380–414:

```cpp
// Поверхностная копия self.* без entity/world.
sol::table snapshot(const sol::table& object) {
    sol::table copy = lua.create_table();
    for (const auto& [key, value] : object) {
        if (!service(key)) copy.raw_set(key, value);
    }
    return copy;
}
bool rawEqual(const sol::object& a, const sol::object& b) {
    lua_State* L = lua.lua_state();
    a.push(L);
    b.push(L);
    const bool equal = lua_rawequal(L, -1, -2) != 0;
    lua_pop(L, 2);
    return equal;
}
// L2: перенос состояния в новый экземпляр. Переносится поле, которое есть в обоих с тем же типом
// и чьё начальное значение в коде не поменяли (иначе правка «cooldown = 5» не была бы видна).
std::size_t transfer(const sol::table& previous, const sol::table& previousInitial, sol::table& next, const sol::table& nextInitial) {
    std::size_t kept = 0;
    for (const auto& [key, value] : previous) {
        if (service(key)) continue;
        const sol::object fresh = next.raw_get<sol::object>(key);
        if (fresh.get_type() == sol::type::lua_nil || fresh.get_type() != value.get_type()) continue;
        const sol::type type = value.get_type();
        if (previousInitial.valid() && (type == sol::type::number || type == sol::type::string || type == sol::type::boolean)) {
            const sol::object before = previousInitial.raw_get<sol::object>(key);
            const sol::object after = nextInitial.raw_get<sol::object>(key);
            if (before.get_type() == type && after.get_type() == type && !rawEqual(before, after)) continue;
        }
        next.raw_set(key, value);
        ++kept;
    }
    return kept;
}
```

1. **стр. 384** `if (!service(key)) copy.raw_set(key, value);` — Снимок: всё из `self`, кроме `entity` и `world`.
2. **стр. 403** `if (fresh.get_type() == sol::type::lua_nil || fresh.get_type() != value.get_type()) con…` — Ключа нет в новом объекте или тип другой — берём новое.
3. **стр. 405** `if (previousInitial.valid() && (type == sol::type::number || type == sol::type::string…` — Для простых значений смотрим, не поменяли ли начальное значение в коде.
4. **стр. 408** `if (before.get_type() == type && after.get_type() == type && !rawEqual(before, after))…` — Поменяли — значит, поле изменилось: остаётся новое.
5. **стр. 410** `next.raw_set(key, value);` — Иначе переносим значение из старого экземпляра.

### Могут спросить

**Чем L1 отличается от L2?**

В обоих режимах каждый живой экземпляр получает новый объект на новом классе и свой `on_create`. L1 оставляет состояние из `on_create`. L2 переносит `self.*` из старого экземпляра — если ключ есть в новом с тем же типом и его начальное значение в коде не поменялось.

**Зачем сравнивать начальные значения?**

Иначе правка `self.timer = 1.0` в `on_create` пропала бы: перенос затёр бы её старым значением. Храним снимок `self` сразу после старого `on_create` и сравниваем со снимком после нового: изменилось — значит, поле поменяли в коде, берём новое. Это и есть «перенос полей, которые не изменились» из ТЗ.

**Что с таблицами и хэндлами в self?**

Их нельзя сравнить по значению (каждый `on_create` создаёт новую таблицу), поэтому переносятся по совпадению типа.

**Когда нужен on_reload(old)?**

Когда автоматика ошибётся: переименовали поле, поменяли формат. Класс объявляет `function Waves:on_reload(old) … end` и сам решает, что взять из старого экземпляра; автоматический перенос тогда не выполняется.

**Что если новый on_create падает?**

Экземпляр остаётся на старом классе, ошибка — в лог. А отключённый раньше runtime-ошибкой экземпляр при успешном reload оживает — в отчёте «revived».

**Вызывается ли on_destroy у старого экземпляра?**

Нет: сущность не уничтожается, меняется только реализация. `on_destroy` мог бы, например, поставить что-то в мир — при reload это было бы лишним побочным эффектом.

## 12. Sandbox: время и память

**Зациклившийся скрипт прерывается через 100 мс, жадный упирается в 256 МБ — движок живёт.**

Это и допфича «изоляция/sandbox», и страховка блока «Стабильность»: падение на скриптовой ошибке стоит баллов.

**Время: count-хук.**
- `lua_sethook(L, hook, LUA_MASKCOUNT, 1000)` — каждые 1000 инструкций VM хук сверяет `steady_clock` с дедлайном.
- Дедлайн ставит RAII-объект `Deadline` на каждый entry point и на загрузку файла (100 мс); вложенный вызов не продлевает внешний.
- `while true do end` в `Core:on_update` → через 100 мс ошибка `assets/scripts/core.lua:16: script exceeded the 100 ms time budget`; Core отключён (failed), Waves и враги работают, редактор живой.
- Обычный `Core:on_update` — ≈ 0,02 мс, ~300 инструкций: хук ни разу не сработал. Вне вызова дедлайн «бесконечность», проверка почти бесплатна.

**Память: свой аллокатор.**
- State создан через `lua_newstate` с нашим аллокатором (`sol::state(panic, allocate, &sandbox)`): считаем занятое и пик.
- Таблица на 40 МБ: `realloc` проходит, пока рост внутри потолка; занято и пик видно в окне Gameplay.
- Рост сверх 256 МБ — `nullptr` → Lua делает аварийную сборку мусора, снова отказ → `LUA_ERRMEM`.
- `string.rep('x', 1 << 30)` просит 1024 МБ → отказ → `Hog:on_update: Lua heap limit exceeded (256 MB)`, экземпляр отключён.
- Сжатие и `free` аллокатор не отказывает никогда — этого требует Lua. После сборки мусора таблицы освобождены, занятое падает.
- `LUA_ERRMEM` не вызывает обработчик сообщений (traceback), поэтому текст ошибки памяти собирает сам `LuaBehaviour`: `Класс:метод: Lua heap limit exceeded (256 MB)`.

`src/scripting/ScriptSystem.cpp`, строки 155–177:

```cpp
void budgetHook(lua_State* L, lua_Debug*) {
    void* data = nullptr;
    lua_getallocf(L, &data);
    const auto& sandbox = *static_cast<Sandbox*>(data);
    if (std::chrono::steady_clock::now() <= sandbox.deadline) return;
    luaL_where(L, 0); // хук не создаёт кадр: уровень 0 — сама зациклившаяся функция
    lua_pushfstring(L, "script exceeded the %d ms time budget", sandbox.budgetMs);
    lua_concat(L, 2);
    lua_error(L);
}
// Бюджет на время вызова. Вложенный вызов (spawn из on_update → on_create) не продлевает внешний.
class Deadline {
public:
    explicit Deadline(Sandbox& sandbox) : sandbox_(sandbox), previous_(sandbox.deadline) {
        sandbox.deadline = std::min(previous_, std::chrono::steady_clock::now() + std::chrono::milliseconds(sandbox.budgetMs));
    }
    ~Deadline() { sandbox_.deadline = previous_; }
    Deadline(const Deadline&) = delete;
    Deadline& operator=(const Deadline&) = delete;
private:
    Sandbox& sandbox_;
    std::chrono::steady_clock::time_point previous_;
};
```

1. **стр. 157** `lua_getallocf(L, &data);` — Sandbox достаём из аллокатора — глобальных переменных не нужно.
2. **стр. 159** `if (std::chrono::steady_clock::now() <= sandbox.deadline) return;` — Вне вызова дедлайн = бесконечность: проверка почти бесплатна.
3. **стр. 160** `luaL_where(L, 0); // хук не создаёт кадр: уровень 0 — сама зациклившаяся функция` — Хук не создаёт кадр: уровень 0 — сама зациклившаяся функция → файл:строка цикла.
4. **стр. 163** `lua_error(L);` — Ошибка Lua внутри protected call — её поймает `LuaBehaviour::call`.
5. **стр. 169** `sandbox.deadline = std::min(previous_, std::chrono::steady_clock::now() + std::chrono::…` — Вложенный вызов не продлевает внешний.
6. **стр. 171** `~Deadline() { sandbox_.deadline = previous_; }` — RAII: после вызова дедлайн возвращается.

`src/scripting/ScriptSystem.cpp`, строки 132–153:

```cpp
struct Sandbox {
    std::size_t used = 0, peak = 0, limit = ScriptLimits{}.memoryBytes;
    int budgetMs = ScriptLimits{}.timeBudgetMs;
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
};
void* allocate(void* data, void* block, size_t oldSize, size_t newSize) {
    auto& sandbox = *static_cast<Sandbox*>(data);
    const size_t old = block ? oldSize : 0; // без блока oldSize — код типа объекта
    if (newSize == 0) {
        std::free(block);
        sandbox.used -= old;
        return nullptr;
    }
    // Lua требует, чтобы сжатие не отказывало; лимит проверяется только при росте.
    if (newSize > old && sandbox.used - old + newSize > sandbox.limit) return nullptr;
    void* next = std::realloc(block, newSize);
    if (next) {
        sandbox.used = sandbox.used - old + newSize;
        sandbox.peak = std::max(sandbox.peak, sandbox.used);
    }
    return next;
}
```

1. **стр. 133** `std::size_t used = 0, peak = 0, limit = ScriptLimits{}.memoryBytes;` — Сколько занято, пик и потолок (256 МБ).
2. **стр. 139** `const size_t old = block ? oldSize : 0; // без блока oldSize — код типа объекта` — Без блока Lua передаёт в oldSize тип объекта — это не размер.
3. **стр. 140** `if (newSize == 0) {` — Освобождение.
4. **стр. 146** `if (newSize > old && sandbox.used - old + newSize > sandbox.limit) return nullptr;` — Рост сверх потолка — отказ; Lua бросит `LUA_ERRMEM`. Сжатие не отказываем никогда.
5. **стр. 150** `sandbox.peak = std::max(sandbox.peak, sandbox.used);` — Пик — для окна Gameplay и теста долгой сессии.

### Могут спросить

**Как прерывается бесконечный цикл?**

`lua_sethook(L, hook, LUA_MASKCOUNT, 1000)`: каждые 1000 инструкций VM хук сверяет время с дедлайном. Дедлайн ставит RAII-объект `Deadline` на каждый entry point и загрузку файла. Превышение — `lua_error`, protected call возвращает ошибку, экземпляр отключается.

**Почему в хуке luaL_where(L, 0), а не 1?**

Хук не создаёт свой кадр стека: уровень 0 — сама зациклившаяся Lua-функция. Поэтому в ошибке — файл и строка цикла: «core.lua:16: script exceeded the 100 ms time budget».

**Не дорого ли проверять время каждые 1000 инструкций?**

Вне вызова дедлайн — «бесконечность», хук выходит после одного сравнения. Внутри — один `steady_clock::now()` (десятки наносекунд) на тысячу инструкций.

**Как ограничена память?**

State создан через `lua_newstate` с нашим аллокатором: считаем занятое и пик; рост сверх 256 МБ — `nullptr`, и Lua бросает `LUA_ERRMEM`. Сжатие никогда не отказываем — этого требует Lua.

**Почему при нехватке памяти нет файл:строка?**

Для `LUA_ERRMEM` Lua не вызывает обработчик ошибок. Поэтому `call` сам добавляет имя вызова: «Hog:on_update: Lua heap limit exceeded (256 MB)».

**Это полноценная изоляция?**

Нет: один state на всех, `_G` общий, вызовы в C++ ограничены только временем. Следующий шаг — отдельный state на мод или сущность.

## 13. Tracy и подсказки в IDE

**Скрипты видно в Tracy поимённо, а в VS Code работает автодополнение по описанию API.**

### Что видно в Tracy

Примерный кадр (16,6 мс):

```
Главный поток  Frame ─────────────────────────────────────────────────────── 16,6 мс
               Input · [Lua: update 0,85 мс] · Physics · Animation · Render · Present
увеличено      Lua: update
               Core · Waves · Enemy ×6 (по ~75 мкс) · lua_gc step
Воркер 3       Lua watcher: scan (0,12 мс) · Lua watcher: validate (0,09 мс)
График         Lua memory KB ≈ 1,8 МБ, ровно
```

- Зоны `Lua: update`, `Lua: start`, `Lua: reload`, `Lua: hot reload`, `Lua: load file`.
- Зона «Lua callback» на каждый entry point с динамическим именем через `ZoneName`: `Enemy:on_update`, `Waves:on_update`. Find zone по имени даёт среднее и медиану тика каждого класса.
- Зоны вотчера `Lua watcher: scan/validate` — на дорожке воркера.
- Графики `Lua memory KB` и `Lua instances`.
- В окне Gameplay: куча Lua (занято, пик, потолок), время скриптов за кадр, бюджет вызова, число отключённых экземпляров.

### Подсказки в IDE

Описание API лежит в `tools/lua-stubs/uzlezz.lua` (`---@class`, `---@param`, `---@return`), `.luarc.json` его подключает, `.vscode/extensions.json` рекомендует расширение `sumneko.lua`. Классы в скриптах помечены `---@class Enemy : uzlezz.Behaviour`, поэтому после `self.entity:` работает автодополнение:

| Префикс | Подсказка | Тип | Что делает |
|---|---|---|---|
| `self.entity:` | `is_alive` | `fun(): boolean` | Жив ли хэндл: мир, сущность и поколение. |
| | `get_position` | `fun(): uzlezz.Vec3` | Позиция копией. |
| | `set_position` | `fun(position: uzlezz.Vec3)` | Записать позицию (числа конечные). |
| | `set_yaw` | `fun(yaw: number)` | Поворот вокруг Z, радианы. |
| | `get_tag` | `fun(): string` | Тег сущности. |
| | `set_animation_speed` | `fun(speed: number)` | Скорость Animator. |
| | `get_field` | `fun(name: string): uzlezz.FieldValue` | Поле поведения из ScriptComponent. |
| | `set_field` | `fun(name: string, value: uzlezz.FieldValue)` | Тип сохраняется: integer ← только целое. |
| | `id` | `integer` | Номер сущности (поле). |
| `self.world:` | `input_pressed` | `fun(action: string): boolean` | Нажато в этом кадре; только при фокусе Game. |
| | `find_by_tag` | `fun(tag: string): uzlezz.Entity` | Первая живая сущность с тегом. |
| | `find_all_by_tag` | `fun(tag: string): uzlezz.Entity[]` | Массив с 1. |
| | `spawn_prefab` | `fun(path: string, position: uzlezz.Vec3): uzlezz.Entity` | Спавн из JSON одной командой. |
| | `destroy` | `fun(entity: uzlezz.Entity)` | Уничтожить после update. |
| | `set_status` | `fun(text: string)` | Строка HUD. |
| `uzlezz.` | `behaviour` | `fun(prototype: T): T` | Объявить класс поведения от C++-базы. |
| | `Vec3` | `fun(x, y, z): uzlezz.Vec3` | Вектор-значение. |
| | `ScriptBehaviour` | `uzlezz.Behaviour` | C++-база: `on_create`, `on_update`, `on_destroy`. |

Если метода нет в описании, lua-language-server подчеркнёт вызов.

Тест читает метатаблицы usertype (`ScriptSystem::apiNames()`) и сверяет их со stubs в обе стороны: забытый или выдуманный метод ломает ctest.

`tools/lua-stubs/uzlezz.lua`, строки 21–56:

```lua
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
```

1. **стр. 23** `---@class uzlezz.Entity` — Аннотация класса для lua-language-server.
2. **стр. 31** `---@return uzlezz.Vec3` — Типы возврата и параметров — для подсказок и проверки.
3. **стр. 55** `---@param value uzlezz.FieldValue` — Псевдоним: boolean | integer | number | string.

### Могут спросить

**Что именно видно в Tracy?**

Зоны `Lua: update`, `start`, `reload`, `hot reload`, `load file`; зона на каждый entry point с именем класса и метода; зоны вотчера `Lua watcher: scan/validate` на дорожке воркера; графики `Lua memory KB` и `Lua instances`.

**Почему ZoneName, а не ZoneScopedN с именем?**

`ZoneScopedN` требует строку-литерал на этапе компиляции. Имя класса известно только в рантайме, поэтому зона одна («Lua callback»), а `ZoneName` подставляет текст на лету. Подписи готовим заранее в конструкторе адаптера — без аллокаций в кадре.

**Попадают ли зоны в тесты?**

Нет: тесты линкуются только с заголовками Tracy без `TRACY_ENABLE`, макросы разворачиваются в ничто.

**Как сделаны подсказки в IDE и почему не разъедутся с кодом?**

Аннотации lua-language-server написаны руками — аналог `pybind11-stubgen` из лекции. `ScriptSystem::apiNames()` читает ключи метатаблиц usertype (без `__*`, `new`, `class_check`, `class_cast`), тест сверяет их со stubs.

**Где ещё видно скрипты без Tracy?**

Окно Gameplay: куча Lua (занято, пик, потолок), время скриптов за кадр, бюджет вызова, число отключённых экземпляров.

## 14. Стабильность

**Сценарии стабильности из ТЗ: что ломаем, что видно в логе и какой тест это держит.**

| Сценарий | Что делает движок | Что в логе | Проверка |
|---|---|---|---|
| Синтаксическая ошибка + reload | старые классы работают, игра идёт | `assets/scripts/enemy.lua:16: 'end' expected` | ScriptSystemTests, ScriptRuntimeTests |
| Ошибка в рантайме | отключён один экземпляр; оживёт после исправления | `enemy.lua:15: Enemy: unknown field 'helth'` | bindingErrorsHaveLocation, hot reload «revived» |
| Бесконечный цикл | прерван через 100 мс | `core.lua:16: script exceeded the 100 ms time budget` | runawayScriptIsInterrupted |
| Скрипт съедает память | аллокация отказана, ошибка скрипта | `Hog:on_update: Lua heap limit exceeded (256 MB)` | heapLimitIsEnforced |
| Серия reload подряд | 200 подряд, рабочий и сломанный по очереди | куча не растёт, экземпляров столько же | hotReloadInPlay |
| Длинная сессия | 30 минут симуляции с волнами | пик кучи после минуты почти не растёт (порог +4 МБ) | ScriptSystemTests |
| Stop / Play много раз | 100 циклов, экземпляры освобождаются | 0 экземпляров после каждого Stop | ScriptSystemTests |
| Корректный выход | stop → `lua_close` → Sandbox | `Lua runtime shut down` · `Shutdown complete` | смоук-запуск редактора |

Живой прогон редактора — правка `waves.lua` во время игры, затем синтаксическая ошибка, затем исправление; редактор не останавливался:

```
[INFO]  Lua: waves.lua reloaded in Play: 1 instance(s), state kept (L2): 5 field(s)
[ERROR] Lua: assets/scripts/waves.lua:20: unexpected symbol near '='
[INFO]  Lua: waves.lua reloaded in Play: 1 instance(s), state kept (L2): 5 field(s)
```

Наборов ctest — 10, все зелёные. В `ScriptRuntimeTests` 8 сценариев: ошибки, лимиты, stubs, reload, вотчер. Руками перед демо: 15–20 минут игры с волнами и Tracy — «Lua memory KB» ровный, «Script time» — доли миллисекунды.

### Могут спросить

**Как вы доказываете, что state не течёт?**

Тест на 30 минут симуляции сравнивает пик кучи после первой минуты и в конце; тест на 200 reload проверяет, что куча не выросла больше чем на 2 МБ. Вживую — график `Lua memory KB` в Tracy и строка Lua heap в окне Gameplay.

**Что будет при ошибке в on_destroy во время Stop?**

Ошибка ловится и уходит в лог, остальные экземпляры всё равно удаляются, Stop завершается.

**Где видно ошибку, кроме лога?**

Окно Gameplay: красная плашка с первой строкой (файл:строка), кнопка «Copy error»; Console: полная запись со стеком Lua.

## 15. Демо и правки правил

**Девять шагов демо за 3 минуты; правки правил — готовы к вставке.**

Запускать из корня репозитория — иначе правки `assets/` движок не увидит. В окне Gameplay: «Reload on save» включён, режим Keep state (L2).

| Время | Шаг |
|---|---|
| 0:00 | **Арена открыта** (`--editor-arena`). Выделить Core — в карточке Script поля из `core.json`. |
| 0:15 | **Превью врага:** Damage 1 → 2, Save to Prefab. Показать `git diff` префаба. |
| 0:30 | **Play** (⌘P), **клик по окну Game**, Space — волна пошла, HUD считает. |
| 0:50 | **F** — импульс уничтожает врагов рядом с ядром. |
| 1:00 | **Правка А в Play:** скорость ×3, ⌘S — враги ускорились, волна и HP не сбросились (L2). |
| 1:30 | **Сломать синтаксис** → в Console «enemy.lua:N: …», игра идёт → исправить → «reloaded». |
| 1:55 | **Правило по просьбе:** Б, В или Г — сохранить, показать в следующей волне. |
| 2:30 | **Наблюдаемость:** Gameplay — Lua heap и Script time; Tracy — зоны `Enemy:on_update`. |
| 2:50 | **Stop** — сцена как до Play. Итог одной фразой. |

Команды:

```bash
./build/mac/GameEngine --editor-arena            # запуск сразу в арену
ctest --test-dir build/mac --output-on-failure   # тесты перед демо
git checkout assets/scripts assets/prefabs       # вернуть файлы после демо
```

### Правка А · число: скорость врагов (живьём в Play)

`enemy.lua`, `on_update` — видно сразу, в той же волне:

```lua
local step = math.min(distance, self.entity:get_field("speed") * 3 * dt)
```

### Правка Б · каждая следующая волна быстрее

`waves.lua`, вместо вызова `spawn_prefab` — число и порядок:

```lua
local enemy = self.world:spawn_prefab(self.entity:get_field("enemy_prefab"),
    uzlezz.Vec3(center.x + math.cos(angle) * radius, center.y + math.sin(angle) * radius, 0.0))
enemy:set_field("speed", 1.5 + 0.5 * (self.wave - 1))
```

### Правка В · условие: импульс, только если в радиусе не меньше трёх врагов

`core.lua`, тело `if … input_pressed("DefensePulse") …`:

```lua
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

### Правка Г · порядок захода врагов

`waves.lua`:

```lua
-- против часовой
local angle = -math.pi / 2 - self.serial * (2 * math.pi / self.wave_count)
-- все с одной стороны, сектор 60°
local angle = -math.pi / 2 + (self.serial / self.wave_count - 0.5) * math.pi / 3
```

### Что сломать для блока стабильности

- **Синтаксис:** удалить `end` в конце `on_update` — «enemy.lua:N: 'end' expected», игра на старом коде.
- **Рантайм:** `get_field("helth")` — «Enemy: unknown field 'helth'», враги замерли; исправить — «revived».
- **Зависание:** `while true do end` в `Core:on_update` — через 100 мс «exceeded the 100 ms time budget», редактор живой.
- **L1:** переключить режим на Reset и сохранить `waves.lua` — номер волны с нуля.

### Ловушки демо

**Space и F не работают — почему?**

Окно Game не в фокусе: ввод доходит до скриптов, только когда по нему кликнули и не идёт ввод текста.

**Правка в enemy.lua не применилась — почему?**

Движок запущен из `build/mac` и читает копию `assets`. Запускать из корня репозитория: `./build/mac/GameEngine`.

**Поменял урон на 1.5 — ошибка. Почему?**

`damage` и `health` — целые поля: «Core: field 'health' expects integer, got number». Брать целые числа.

**Поменял поле превью врага, а враги прежние?**

Враги создаются из `enemy.json` на диске — нужен Save to Prefab.

## 16. Лекция 5, часть 1: объектная модель, ссылки, обновление

**Три вопроса, которые лекция задаёт любому движку: как хранить объекты, как на них ссылаться, в каком порядке обновлять.**

### Объект или свойство?

**Object-centric** — объект = экземпляр класса, атрибуты и методы вместе (знакомый ООП-взгляд; беда — глубокие иерархии):

```
Object1: Position = (0, 3, 15),  Orientation = (0, 43, 0)
Object2: Position = (−12, 0, 8), Health = 15
Object3: Orientation = (0, −87, 10)
```

**Property-centric** — объект = id, свойство = таблица «id → значение», как в БД:

```
Position:    Object1 = (0, 3, 15),   Object2 = (−12, 0, 8)
Orientation: Object1 = (0, 43, 0),   Object3 = (0, −87, 10)
Health:      Object2 = 15
```

**У нас:** `World` держит `unordered_map<Entity, T>` на каждый тип компонента — это property-centric / ECS; `ScriptComponent` — аналог свойства `ScriptId`.

### Объект удалили. Что со ссылкой?

| Ссылка | Что будет | Комментарий |
|---|---|---|
| Сырой указатель | висячий указатель | Чтение освобождённой памяти — мусор или краш. При релокации памяти указатели неприменимы вовсе. |
| `shared_ptr` | объект не умирает | Пока жива ссылка, объект висит «осиротевшим»: утечка и удаление в неожиданный момент. Плюс атомарный счётчик на каждую копию. |
| `weak_ptr` | `lock()` вернёт пусто | Безопасно, но нужен control block и подсчёт ссылок; «легко написать — трудно написать правильно». |
| handle + id (у нас) | таблица скажет «нет такой» | Индекс в таблице + уникальный id (у нас — поколение и токен мира): `is_alive()` = false, остальные методы — понятная ошибка. Переживает релокацию. |

### Как обновлять объекты

| Модель | Кадр | Вывод |
|---|---|---|
| `Update()` у каждого | для каждого объекта `obj->Update(dt)`, внутри — вызовы анимации, физики, рендера | ломает пакетную обработку и порядок кадра |
| Batched | главный цикл: `Animation.update(всех)` → `Physics.update(всех)` → `Render.submit(всех)` | кэш-когерентность, меньше дублирования и реаллокаций, pipelining; так устроены системы ECS — у нас тоже |
| Phased (Naughty Dog) | Update 1 — до блендинга → блендинг → Update 2 — до финальной позы → финальная поза → Update 3 | несколько точек апдейта за кадр через колбеки; у нас скрипты — одна фаза до физики |
| Buckets | bucket 0 — независимые; bucket 1 — зависят от 0; bucket 2 — от 0…1 | внутри bucket — параллельно и через снимки; one-frame-off lag лечат state caching и метки времени |

### Могут спросить · лекция 5, часть 1

**Статика и динамика мира — чем занимается gameplay-слой?**

Статика (ландшафт, здания, дороги) грузится с чанком и не меняется. Динамика (персонажи, транспорт, оружие, эмиттеры) спавнится и уничтожается в рантайме — это и есть работа gameplay-слоя. Чем больше доля динамики, тем «живее» мир и дороже объектная модель. *(Л5 сл. 3 · Грегори 15.1.1)*

**Что такое высокоуровневый поток игры (game flow)?**

Последовательность, дерево или граф целей игрока: задания, этапы, уровни, волны; критерий успеха каждой цели; штраф за провал; кат-сцены (IGC, NIS). Цели группируются в главы или акты. У нас волны — простейший game flow. *(Л5 сл. 4)*

**Топологии мировых чанков**

- **Звезда:** хаб и уровни-лучи, грузим по одному чанку.
- **Граф:** чанки связаны произвольно, переходы — двери, коридоры, «воздушные шлюзы».
- **Открытый мир:** сетка чанков, сильный LOD, непрерывный стриминг без экранов загрузки.

*(Л5 сл. 5)*

**Tool-side и runtime объектные модели — в чём разница?**

Tool-side — типы объектов, которые видит дизайнер в редакторе. Runtime — языковые конструкции и системы, которыми программисты это реализовали. Они могут совпадать, отображаться друг на друга или полностью различаться; spawner'ы и type schemas их развязывают. *(Л5 сл. 6)*

**Data-driven движок и почему «осторожно»**

Data-driven — поведением управляют данные художников и дизайнеров, а не только код. Осторожно, потому что каждая data-driven система — новый «язык», который надо документировать, отлаживать и поддерживать годами; лишняя generic-ность делает движок медленнее и непонятнее. Главный критерий — KISS. *(Л5 сл. 7 · Л1 сл. 12)*

**Из чего состоит gameplay foundation system?**

Шесть подсистем: runtime объектная модель (типы, спавн, запросы, ссылки); управление уровнями и стриминг; обновление объектов (порядок, зависимости); события и сообщения; скриптинг; цели и поток игры. *(Л5 сл. 8)*

**Что умеет runtime object model (список)?**

Динамический спавн и уничтожение; связь с низкоуровневыми системами; поведение в реальном времени; определение новых типов; уникальные id; запросы к объектам; ссылки на объекты; конечные автоматы; сетевая репликация; сохранение и загрузка. *(Л5 сл. 9)*

**Object-centric и property-centric модели**

**Object-centric:** tool-side объект = экземпляр класса, атрибуты и поведение инкапсулированы — объект как «существительное» с методами.

**Property-centric:** объект = только id, свойства лежат в таблицах по типу свойства с ключом id — как реляционная БД. Поведение — в классах свойств или в скрипте.

*(Л5 сл. 10, 15)*

**Чему учит пример Hydro Thunder?**

Аркадные гонки (Midway, 1999) на чистом C без наследования: `World_t` хранит мир, вся динамика — экземпляры одной `WorldOb_t`. Простая модель работает для простой игры: сложность архитектуры растёт вместе со сложностью игры, не раньше. *(Л5 сл. 11)*

**Болезни глубоких иерархий классов**

- Трудно понимать и менять: правка в середине дерева задевает всех наследников.
- Многомерные таксономии: одно дерево пытается ответить «движется? рендерится? сталкивается?».
- Deadly diamond от множественного наследования; mixins — ограничение, а не решение.
- Bubble-up effect: функции всплывают в базовый класс, он раздувается.

Вывод: наследование — для «является», остальное — композиция.

*(Л5 сл. 12)*

**Композиция и агрегация — разница**

Композиция: A содержит B и владеет его временем жизни («has-a»). Агрегация: связь указателем или ссылкой без владения. GameObject раскладывают на независимые классы с одной службой: Movable, Renderable, Collidable, Animating, Physical. *(Л5 сл. 13)*

**Generic-компоненты и pure component model**

**Generic:** GameObject — хаб, владеет компонентами и маршрутизирует сообщения. **Pure:** центрального объекта нет, компоненты связаны общим id. Собирать объект — фабрикой или данными. Главная цена — межкомпонентная коммуникация: «владельца», который знает всех, больше нет. *(Л5 сл. 14)*

**Откуда поведение в property-centric мире?**

Путь 1 — классы свойств: каждый тип свойства — класс с методами. Путь 2 — скрипт: значения — сырые данные, поведение — скрипт, на который указывает свойство `ScriptId`. Отсюда скриптинг входит в архитектуру: данные отделены от поведения — поведение отдают языку, который правится без пересборки. *(Л5 сл. 16)*

**Свойства и компоненты — в чём отличие?**

Свойство — атрибут самого объекта (здоровье, инвентарь, способность). Компонент — связка с подсистемой движка (рендер, анимация, коллизии). На практике смешивают: компонент как «порт» в подсистему + свойства как его данные; ECS — продолжение этой идеи. *(Л5 сл. 17)*

**Property-centric: за и против**

**За:** экономнее по памяти; естественно собирается из данных; cache-friendly — данные одного типа подряд (SoA вместо AoS).

**Против:** связи между свойствами трудно гарантировать; сложнее отлаживать; обращение к памяти дорожает, когда данные объекта разбросаны по таблицам.

*(Л5 сл. 18)*

**ECS на примере EnTT**

Header-only C++. Entity — только id, component — чистые данные, system — код, идущий по `view` (`registry.view()` → `each`). *(Л5 сл. 19)*

**Как хранить объекты: бинарные образы, сериализация, spawners**

**Бинарные образы** «как в памяти» — быстро, но ломаются об указатели, vtable и endianness. **Сериализация** (XML/JSON, SerializeIn/Out или рефлексия) — но C++ не умеет создать класс по строке-имени. **Spawners + type schemas** разрывают связку «редактор ↔ рантайм». *(Л5 сл. 20)*

**Spawner и type schema — определения**

**Spawner** — лёгкое data-only описание объекта: id tool-side типа + key-value начальных атрибутов. **Type schema** — описание типа: атрибуты, их типы, значения по умолчанию, какой GUI-элемент показать в редакторе; атрибуты по умолчанию в данных опускаются. Пример — YAML-сцены и префабы Unity. У нас: JSON-префаб ≈ spawner, `fields` в Lua ≈ type schema. *(Л5 сл. 21)*

**Загрузка и стриминг мира: три схемы**

- **Простой уровень:** один чанк; сквозные ресурсы LSR (load and stay resident) внизу стека памяти, чанк поверх.
- **«Воздушный шлюз»:** пока играют в A, B грузится в отдельный блок в фоне; маленький чанк-шлюз закрывает стык.
- **Стриминг:** данные во время игры — чанками по кольцу буферов или блоками одного размера под pool-аллокатор.

*(Л5 сл. 22)*

**Память под спавн и сохранения**

Аллокация в геймплее медленная и фрагментирует память. Варианты: всё спавнить при загрузке чанка; пул на тип объекта; placement new / VirtualAlloc; small memory allocators; релокация блоков.

Сейвы: чанк задаёт начальное состояние, сейв — только текущее. Checkpoints — имя точки + состояние игрока; save anywhere — состояние всех значимых объектов (≈ чанк минус статика).

*(Л5 сл. 23)*

**Ссылки на объекты: указатели, smart pointers, handles**

- **Сырые указатели:** orphaned objects, stale и invalid pointers; при релокации памяти неприменимы.
- **Smart pointers** (scoped, shared, weak, intrusive): указатель + подсчёт ссылок; легко написать, трудно написать правильно.
- **Handles:** индекс в глобальной таблице указателей; переживает релокацию; против stale — уникальный id внутри хэндла.

У нас — handle с поколением.

*(Л5 сл. 24)*

**Запросы к объектам и чем их ускорять**

По id — хэш-таблица или дерево. По критерию — заранее отсортированные списки. Лучи и объёмы — через коллизии (ray, sphere, convex cast). Объекты в регионе или радиусе — пространственное хэширование. Типовые запросы: враги в видимости, все объекты типа, урон в радиусе взрыва. *(Л5 сл. 25)*

**Почему «Update() у каждого объекта» не работает?**

Объекты дёргают подсистемы со своим состоянием, обновляемым раз в кадр. Вызов подсистемы прямо из `Update()` объекта разбивает пакетную обработку и порядок кадра. Поэтому подсистемы обновляет главный цикл (batched). *(Л5 сл. 26–27)*

**Batched updates — что дают?**

Кэш-когерентность (данные одного типа подряд), минимум дублирующихся вычислений, меньше реаллокаций, эффективный pipelining; scatter/gather раскидывает работу по ядрам. *(Л5 сл. 28)*

**Phased updates — пример Naughty Dog**

Объекты зависят от промежуточных результатов подсистем, поэтому получают несколько точек апдейта за кадр. У Naughty Dog — трижды: до блендинга анимации, после блендинга до финальной позы, после финальной позы. Реализация — колбеки, которые клиенты регистрируют на фазы; анимация ничего не знает об игровых объектах. *(Л5 сл. 29)*

**Buckets, one-frame-off lag, state caching, time-stamping**

**Buckets:** объекты bucket B зависят только от buckets 0…B−1. **One-frame-off lag:** во время цикла апдейта состояния несогласованы, объект может прочитать чужое состояние прошлого кадра. **State caching:** хранить прошлое состояние рядом с новым — любой читает согласованное прошлое, бонус — интерполяция (Havok). **Time-stamping:** метка, из какого кадра данные. *(Л5 сл. 30–31)*

**Как параллелить апдейт объектов?**

Buckets как задачи (bucket B читает только &lt; B) или явный граф зависимостей, циклы разрывать. Вместо локов — snapshots: в начале bucket'а каждый публикует снимок, запросы идут к снимкам. Мутации чужого состояния — между bucket'ами; внутри — под локом, через очередь мутаций (разбор после bucket'а) или job-синхронизатор. Глобальный лок убил бы параллелизм. *(Л5 сл. 33)*

## 17. Лекция 5, часть 2: события и скриптинг

**События, скриптовые языки и встраивание: где на спектре стоит наш движок.**

### Спектр архитектур скриптинга (от меньшего скрипта к большему)

1. **Scripted callbacks** — скрипт вызывается на отдельные события.
2. **Scripted event handlers** — обработчики событий целиком на скрипте.
3. **Расширение типов** — новые типы объектов объявляются скриптом. *У нас: `uzlezz.behaviour`.*
4. **Scripted components / properties** — поведение компонента на скрипте. *У нас: ScriptComponent + класс поведения.*
5. **Script-driven engine** — скрипт управляет подсистемами движка.
6. **Script-driven game** — вся игра на скрипте, движок только исполняет.

Наш движок — на уровнях 3–4: физика, рендер и анимация остаются в C++.

### Взрыв ранил трёх, один умер: как доставить события

**Немедленно** — просто и предсказуемо, но стек растёт, а каждый обработчик обязан быть реентерабельным. Так работает `EventDispatcher` нашего движка.

```
Explosion.dispatch()
├ Soldier1.OnDamage(40)
├ Soldier2.OnDamage(40)
│  └ Soldier2.OnDied()              ← обработчик породил событие
│     └ Squad.OnMemberLost()        ← стек растёт
│        └ HUD.OnSquadChanged()
└ Soldier3.OnDamage(40)            ← а если HUD тронул Soldier3?
```

**Очередь** — контроль момента обработки, постинг «в будущее», приоритеты. Цена: копирование событий и аргументов, память под очередь, трудная отладка.

```
кадр 1  очередь: [Explosion]
        Explosion → post Damage(S1, S2, S3)
        очередь: [Damage S1] [Damage S2] [Damage S3]
        S2: hp ≤ 0 → post Died(S2), приоритет 1
кадр 2  очередь: [Died S2 · p1] [Respawn · t+3 с]
        Squad.OnMemberLost → post SquadChanged
кадр 2+ HUD.OnSquadChanged          ← стек плоский
```

### Lua, Python, C#

| Критерий | Lua | Python | C# |
|---|---|---|---|
| Размер рантайма | ~200 КБ, ~15 тыс. строк ANSI C | ~10 МБ (embeddable) | десятки МБ (Mono / CoreCLR) |
| Скорость скрипта | высокая (LuaJIT) | умеренная | высокая (JIT) |
| Порог входа / экосистема | низкий / узкая | низкий / огромная | средний / широкая |
| Как встраивают | C API, sol2 | C API → Boost.Python → pybind11 | Mono или CoreCLR hosting |

### Могут спросить · лекция 5, часть 2

**Почему события, а не просто вызов метода OnExplosion()?**

Вызов метода — статически типизированное позднее связывание: набор событий и обработчиков зашит в код. Нужна динамически типизированная поздняя привязка — менять события и обработчики данными, без перекомпиляции. *(Л5 сл. 34)*

**Событие как объект: зачем и как кодировать тип?**

Событие = тип + аргументы. Объект даёт единый обработчик, персистентность (очередь, сохранение) и «слепую» пересылку (blind forwarding). Тип: **enum** — дёшево и хрупко (централизован, порядок значим); **строка** — опечатки, память; **хэшированная строка** (FName в Unreal) — читаемость в разработке, сравнение чисел в рантайме. *(Л5 сл. 35)*

**Как передавать аргументы событий?**

Класс на тип события (типизированные геттеры вроде `GetHealthPack()`) — безопасно, но много классов. Массив variant'ов — порядок аргументов значим и легко ломается. Key-value пары — по имени, без порядка, но с кастами на стороне приёмника. *(Л5 сл. 36)*

**Как событие доходит до нужных объектов?**

По графу отношений (иерархия, владение, группы) — паттерн Chain of Responsibility: обработать или передать дальше (урон: хитбокс → персонаж → отряд). Чтобы не слать всем всё — регистрация интереса: доставка только подписчикам типа события. *(Л5 сл. 37)*

**Очередь событий или немедленная обработка?**

**Очередь:** + контроль момента обработки, постинг «в будущее», приоритеты при равном времени; − сложность, глубокое копирование событий и аргументов, динамическая память, трудная отладка и гонки.

**Немедленно:** просто, но обработчик порождает новые события — стек растёт; каждый обработчик обязан быть реентерабельным.

*(Л5 сл. 38)*

**Data-driven события и GUI-программирование для дизайнеров**

Полный скриптинг у дизайнеров даёт больше багов, поэтому многие берут середину — визуальные инструменты, потоки данных между объектами (data pathway). За: простота, подсказки, проверка ошибок. Против: дорого разрабатывать и поддерживать, дизайнер ограничен инструментом. *(Л5 сл. 39)*

**Что такое скриптовый язык в движке? Data-definition vs runtime scripting**

Язык, чтобы пользователи управляли и настраивали поведение приложения (VBA в Excel, MEL/Python в Maya). В движке — высокоуровневый, простой, с удобным доступом к движку. Data-definition — описывать данные для движка; runtime scripting — исполняется внутри движка. Отличия игрового языка: интерпретируемый, лёгкий, быстрая итерация без пересборки, простота. *(Л5 сл. 40)*

**Свой язык или готовый: QuakeC, UnrealScript, Pawn**

- **QuakeC** — упрощённый C с хуками в Quake, без указателей; Кармак сделал язык под одну игру и получил моддинг-экосистему.
- **UnrealScript** — C++-подобный, классы, FName, ссылки без свободных указателей; ООП удобно геймплейщикам, прожил три поколения Unreal.
- **Pawn/Small** — C-подобный, байт-код для крошечной VM, конечные автоматы; маленький footprint важнее богатства языка.

Свой язык = своя VM, тулчейн, баги и ни одного учебника.

*(Л5 сл. 42)*

**Факты про Lua из лекции**

Родился в 1993 как язык конфигурации и описания данных. Интерпретируемый, динамическая типизация, сборка мусора, замыкания, корутины. ~200 КБ, ~15 тыс. строк ANSI C; с LuaJIT — один из самых быстрых интерпретируемых; портируется от Symbian до PS3. Единственная структура данных — table, «классы» — через метатаблицы (пример: `__add`, `__tostring`). *(Л5 сл. 43)*

**Lua vs Python vs C# — таблица из лекции**

Размер рантайма: Lua ~200 КБ, Python ~10 МБ (embeddable), C# — десятки МБ. Скорость: Lua высокая (LuaJIT), Python умеренная, C# высокая (JIT). Порог/экосистема: Lua низкий/узкая, Python низкий/огромная, C# средний/широкая. C# встраивают через Mono или CoreCLR; оправдан, если команда уже пишет на нём. *(Л5 сл. 44–45)*

**Спектр архитектур скриптинга (от меньшего к большему)**

1. Scripted callbacks — скрипт на отдельные события.
2. Scripted event handlers — обработчики целиком на скрипте.
3. Расширение типов — новые типы объектов скриптом.
4. Scripted components/properties — поведение компонента на скрипте.
5. Script-driven engine — скрипт управляет подсистемами.
6. Script-driven game — вся игра на скрипте.

Главное назначение — gameplay-фичи поверх объектной модели. Место на спектре — архитектурное решение.

*(Л5 сл. 46)*

**Как VM стыкуется с движком: DC от Naughty Dog**

DC — диалект Scheme (Lisp) в Uncharted/TLOU. Исполняемые куски — script lambdas с уникальными именами. Внутри VM: банк регистров variant-типа, стек кадров (`DcPushStackFrame`/`DcPopStackFrame`), `DcLookUpByteCode` ищет лямбду по имени. Нативные функции ищутся по имени в глобальной таблице, которую задают программисты движка. *(Л5 сл. 47)*

**Как скрипт ссылается на нативные объекты (по лекции)?**

Сырой указатель отдавать нельзя. Варианты: opaque numeric handle (число, значимое только для движка, + уникальный id против stale); строковые хэндлы (читаемо, но дороже); хэшированные строки/символы (в DC `'foo` ↔ `SID("foo")`). *(Л5 сл. 48)*

**Многопоточность в скриптах**

Несколько скриптов «одновременно» — кооперативно: скрипт сам засыпает (sleep/yield) в ожидании события и просыпается, когда оно наступило. В Lua это корутины. GIL и вызов скриптов из job-системы в лекции не разбирались. *(Л5 сл. 49, 67)*

**Embedding и extension — в чём разница?**

**Embedding:** движок — хозяин процесса, интерпретатор слинкован в приложение; C++ исполняет скрипт. **Extension:** процессом владеет интерпретатор, C++ живёт в модуле. Механика перевода значений та же, разница — кто `main()`. Для движка почти всегда embedding — как у нас. *(Л5 сл. 51)*

**Чем биндить Python: C API → Boost.Python → pybind11**

C API — всё руками (подсчёт ссылок, упаковка, ошибки). Boost.Python — шаблоны вместо рутины, но тащит весь Boost. pybind11 — header-only, современный C++, минимум кода. Для Lua аналог — sol2 (header-only, usertypes). *(Л5 сл. 52)*

**Что умеет pybind11 из коробки?**

Функции с пользовательскими типами (по значению, ссылке, указателю), перегрузки, методы экземпляра и статические, одиночное и множественное наследование, расширение виртуальных классов C++ в Python, enum, колбеки-лямбды, итераторы, STL, умные указатели, внутренние ссылки с подсчётом. *(Л5 сл. 53)*

**Пример «Deep»: один объект или копия?**

Адрес Python-обёртки и адрес C++-объекта разные, но оба раза C++ печатает один и тот же `this`: `py::cast(&eng)` — обёртка указывает на тот же экземпляр, не копию. Это тот же вопрос владения, что у handles, — только решает библиотека. *(Л5 сл. 56)*

**Singleton в pybind11: зачем return_value_policy::reference?**

`def_static("getInstance", ..., py::return_value_policy::reference)` — Python получает ссылку и **не владеет** объектом, иначе попытался бы удалить синглтон. Состояние разделено между языками. Путь к скриптам — `sys.path.append` через `py::exec`, от рабочей папки. *(Л5 сл. 57)*

**Trampoline (трамплин) в pybind11: как работает?**

Класс-наследник `PyBehaviourTrampoline` переопределяет каждую виртуальную функцию макросом `PYBIND11_OVERRIDE(ret, Parent, Name)`: виртуальный вызов из C++ уходит в интерпретатор, если Python-класс метод переопределил. `super().OnUpdate()` зовёт нативную реализацию. Для чисто виртуальных — `PYBIND11_OVERRIDE_PURE`. Регистрация: `py::class_`. *(Л5 сл. 58)*

**Как движок находит класс в скрипте, не зная имени?**

Python рефлексивен: helper на Python через `inspect.getmembers` + `issubclass` + `obj.__module__` находит наследника `PyBehaviour` в модуле; C++ его вызывает. Приём «логика о языке — на самом языке». У нас имя класса задано в префабе (`class`), и мы проверяем `base`. *(Л5 сл. 59)*

**Smart holder: зачем py::classh и trampoline_self_life_support?**

Пример «Deeper» падал с «Try to Call Pure Virtual Function»: Python-объект умер, а C++ держал его как компонент. `py::classh` (ветка smart_holder) позволяет один объект передавать как `unique_ptr` и `shared_ptr` в обе стороны, освобождает `unique_ptr` при передаче Python → C++ и привязывает время жизни Python-части трамплина к умному указателю (`py::trampoline_self_life_support`). *(Л5 сл. 60–62)*

**Ошибки пользовательского кода в pybind11**

Ловим `py::error_already_set` вокруг вызова — `e.what()` содержит тип ошибки, сообщение и место (файл и строку). У нас аналог — protected call sol2 + traceback-хэндлер. *(Л5 сл. 63)*

**Hot reload в примере с pybind11**

По команде: `useBeh.reload()` → заново взять класс из модуля → создать новый экземпляр → вызвать. Состояние старого экземпляра не переносится (это L1). У нас — вотчер вместо команды и ещё перенос состояния L2. *(Л5 сл. 64)*

**Stubs для Python**

`pybind11-stubgen` генерирует `.pyi` с сигнатурами (`def incA(self) -> None: ...`) — IDE даёт автодополнение и типы. У нас то же для Lua: аннотации lua-language-server руками плюс тест сверки с биндингами. *(Л5 сл. 65)*

**Дистрибуция Python у пользователя**

Поставить с собой: Windows embeddable package — `python.zip` + `python.dll` в папку билда, для 3.11 ≈ 9,6 МБ, полная изоляция. Искать в системе: подойдёт Python из PATH, даже новее, — годится для инструментов, не для игрока. *(Л5 сл. 66)*

**Что в лекции 5 не рассказывали (могут спросить «сами»)?**

GIL и вызов скриптов из job-системы; сериализация Python-объектов (pickle небезопасен, `__dict__`/`__slots__`, `__getstate__`/`__setstate__`); отладка скриптов (debugpy, брейкпоинты в чужом процессе); annotated-биндинги. *(Л5 сл. 67)*

## 18. Лекции 1–4 кратко

Вопросы по ротации идут «по всем лекциям»: ключевые факты и вопросы по каждой.

### Лекция 1 · Карта движка

Слои Runtime Engine Architecture (Грегори), сверху вниз:

1. **Game-specific** — ИИ, поиск пути.
2. **Gameplay foundation** — объекты, события, **скриптинг**, стриминг, сейвы.
3. **Подсистемы** — рендер · физика · анимация · HID · аудио · сеть · профилирование.
4. **Менеджер ресурсов.**
5. **Core systems** — assert, память, RTTI, математика, контейнеры.
6. **Platform independence layer.**
7. **SDK и middleware** — OpenGL/DX12/Vulkan/Metal · Havok/PhysX/Jolt · STL/Boost/EASTL.
8. **ОС · драйверы (UMD/KMD) · железо.**

Карта читается снизу вверх. Наша ЛР 2 — слой gameplay foundation: объектная модель (ECS), скриптинг, шаблонный спавн.

Оценка: 5 лаб — 100 баллов (ЛР 2: 8 + 4 + 4, +2 вовремя); команда ≥ 60 / 75 / 90. Ротация: двое не-презентаторов, 0/1/2, сумма шести лучших ≥ 6 / 9 / 11.

**Система оценивания курса**

5 лаб: Job System 24 (12/6/6), Скриптинг 16 (8/4/4), Рендер 24, Подсистема на выбор 24, Краш-репорты 12 — сумма 100; +2 за демо вовремя. Команда: ≥60 → 3, ≥75 → 4, ≥90 → 5. АИД-2: 9 демо-дней, отвечают двое не-презентаторов, ответ 0/1/2, итог — сумма шести лучших: ≥6 → 3, ≥9 → 4, ≥11 → 5. Баллы не сгорают. *(Л1 сл. 6–8)*

**Что такое игра и видеоигра?**

Теория игр: агенты выбирают стратегии, чтобы максимизировать выгоду в рамках правил. Koster: интерактивный опыт со всё более сложными паттернами, которые игрок осваивает. Видеоигра — мульти-агентное интерактивное компьютерное моделирование в режиме **мягкого реального времени** (срыв дедлайна не катастрофа). *(Л1 сл. 10–11)*

**Что такое игровой движок?**

Расширяемое ПО, которое служит основой для многих разных игр без серьёзных модификаций. От игры движок отличает архитектура, управляемая данными (data-driven). *(Л1 сл. 12)*

**Слои Runtime Engine Architecture (Грегори), снизу вверх**

Железо → драйверы → ОС → SDK и middleware → platform independence layer → core systems (assert, память, RTTI, математика, контейнеры) → менеджер ресурсов → рендер (low-level/RHI, scene graph/culling, VFX, front end) → профилирование и отладка, коллизии и физика, анимация, HID, аудио, сеть → gameplay foundation (объектная модель, события, скриптинг, стриминг, сейвы) → game-specific (ИИ). Сбоку — инструменты (DCC) и редактор мира. *(Л1 сл. 15–39)*

**UMD и KMD — что это?**

User-mode driver (nvd3dum.dll, atiumd*.dll) — большая часть «магии» на CPU, компиляция шейдеров. Kernel-mode driver — работа с железом: выделение и отображение физической памяти, буфер команд. *(Л1 сл. 17)*

**Middleware: графика, физика, библиотеки**

Графика: OpenGL (deprecated на macOS), DirectX 12, Vulkan (явные списки команд, контроль памяти), Metal (единственный на iOS/macOS), WebGPU. Физика: Havok, PhysX (open source с 2018), Jolt, Bullet. Библиотеки: STL, Boost, EASTL; крупные движки пишут свои контейнеры ради памяти и скорости. *(Л1 сл. 20–22)*

**Gameplay foundation на карте движка — ключевой вопрос**

Модель объектов, события, скриптинг, стриминг мира, сохранения. Ключевой вопрос дизайна: наследование или композиция (ECS) — где живёт логика объекта. В коде: Godot SceneTree, UE GameFramework, O3DE AzFramework/Entity. *(Л1 сл. 36)*

**Жизнь кадра (UE Insights)**

Чтение ввода → обновление мира (проход по компонентам) → отправка на отрисовку → UI → ожидание GPU. Редактор мира желательно рендерит ту же сцену тем же движком; тренд — редактор как расширение движка. *(Л1 сл. 39–40)*

### Лекция 2 · Профилирование и методика замеров

**Закон Амдала:** `S = 1 / ((1 − p) + p / n)`, где p — доля функции в кадре, n — во сколько раз её ускорили.

| p | n | S | кадр быстрее на |
|---|---|---|---|
| 2% | 10× | 1,018 | 1,8% |
| 10% | 10× | 1,099 | 9,9% |
| 50% | 10× | 1,818 | 81,8% |
| 50% | ∞ | 2,0 | 100% |

Сначала профиль: оптимизируют верхние строки трейса, а не любимые функции.

| FPS | Бюджет кадра | Где норма |
|---|---|---|
| 30 | 33,3 мс | консоли «качество», мобильные |
| 60 | 16,6 мс | стандарт |
| 90 | 11,1 мс | VR |
| 120+ | ≤ 8,3 мс | киберспорт |

frame time ≈ max(CPU, GPU) — конвейер; задержка ≈ CPU + GPU. 1 мс на 10 000 объектов = 100 нс ≈ 300 тактов.

**Кадровые бюджеты (бюджет кадра)**

30 FPS — 33,3 мс (консоли «качество», мобильные); 60 — 16,6 мс (стандарт); 90 — 11,1 мс (VR); 120+ — 8,3 мс и меньше (киберспорт). Бюджет — на всё: логика, физика, анимация, submit. *(Л2 сл. 4)*

**CPU и GPU: frame time и latency**

CPU готовит кадр N+1, пока GPU рисует N — конвейер: frame time ≈ max(CPU, GPU), кадр держится на медленном. Сумма CPU + GPU важна для задержки input-to-photon: конвейер даёт FPS, но добавляет кадр задержки — в VR глубину конвейера режут. *(Л2 сл. 5)*

**Цена одной миллисекунды**

1 мс на 10 000 объектов = 100 нс на объект ≈ 300 тактов. Один лишний cache miss на объект — и бюджет съеден. Считать в наносекундах на сущность, а не «на глаз». *(Л2 сл. 6)*

**Закон Амдала**

`S = 1 / ((1 − p) + p / n)`. Ускорили функцию в 10 раз (n = 10), а она занимает 2% кадра (p = 0,02): S ≈ 1,018 — всего +1,8% FPS. Вывод: сначала профиль, оптимизируют верхние строки трейса. *(Л2 сл. 7)*

**«Преждевременная оптимизация…» (Кнут) — в чём нюанс?**

Полная цитата Кнута — «…in 97% of cases». Преждевременны (только по профилю): микрооптимизации, ручной инлайнинг, битовые трюки, SIMD «на всякий случай». Закладываются заранее: job system, layout данных (SoA/AoS), бюджеты подсистем, асинхронная загрузка. Архитектура — не преждевременная оптимизация. *(Л2 сл. 8)*

**Методика замеров**

Цикл: измерить (базлайн на фиксированном сценарии, цифры в лог) → гипотеза (одна, проверяемая) → правка (одна за итерацию) → замер (тот же сценарий и сборка). Условия: Release с debug info, прогрев, фиксированная сцена, N ≥ 5–10 прогонов (в ЛР ≥ 3), медиана и p95/p99 — среднее прячет фризы: «60 FPS» может быть 55 кадров по 16 мс + 5 по 100 мс. *(Л2 сл. 9–10)*

**Ловушки замеров**

Debug vs Release (инлайнинг, checked-итераторы MSVC, assert'ы — разница в разы); компилятор выкидывает мёртвый код в микробенче; vsync маскирует правду (60 FPS ≠ 16,6 мс работы); троттлинг (батарея, турбо, перегрев); фон. Странная цифра — сначала ищи ошибку в методике. *(Л2 сл. 11)*

**Перцентили: p50, p90, p95, p99**

pN — значение, ниже которого лежат N% замеров (по отсортированному массиву). p50 = медиана, устойчива к выбросам. p90 — хуже лишь 1 кадр из 10; p95 — 1 из 20 (при 60 FPS ≈ 3 кадра в секунду); p99 — 1 из 100, ловит хвост — фризы. *(Л2 сл. 19 (заметки))*

**Sampling vs instrumentation**

Sampling периодически снимает стеки: видит «где время» без правок кода, но пропускает короткие функции и привязку к кадру (perf, VTune, Very Sleepy). Instrumentation — код сам метит зоны: точные границы, кадры, события, но видит только размеченное (Tracy, PIX, Unreal Insights). Для кадра удобнее instrumentation. *(Л2 сл. 13)*

**Почему Tracy и как он устроен?**

Наносекундные зоны с оверхедом в единицы нс — можно профилировать Release; frame graph из одного `FrameMark`; аллокации, локи, сообщения, графики; GPU-зоны; open source. Клиент-сервер: `ZoneScoped` пишет в lock-free очередь, отдельный поток стримит по сети, тяжёлый GUI вне процесса — не влияет на замер, можно профилировать другую машину. Без `TRACY_ENABLE` макросы пустые. *(Л2 сл. 14–16)*

**Макросы Tracy, которые надо знать**

`ZoneScoped`/`N`/`C` — зона, имя, цвет; `ZoneText`/`ZoneValue` — текст и число в зону; `ZoneNamed`/`ZoneTransient` — ручные и динамические имена; `FrameMark`; `TracyPlot` — график; `TracyAlloc`/`TracyFree` в глобальных new/delete (malloc не ловится); `TracyLockable` — contention мьютекса; `TracyMessage` — лог на таймлайне; GPU: `TracyD3D12Zone`, `FrameImage`. *(Л2 сл. 19–23)*

**Оверхед Tracy и sampling в нём**

Зона — единицы–десятки нс, миллионы зон в секунду — норма. Цикл на 10 000 итераций размечать снаружи, не по итерации. Sampling: ОС снимает стеки (`TRACY_SAMPLING_HZ`, `TRACY_NO_SAMPLING`), видны код без зон и context switches, ghost zones. *(Л2 сл. 24–25)*

**CPU-bound или GPU-bound — как проверить?**

Тест 1: GPU-зоны в Tracy — GPU time ≈ frame time → GPU-bound; GPU простаивает → CPU-bound. Тест 2: снизили разрешение, FPS вырос → GPU-bound, не вырос → CPU-bound. Оптимизировать ограничивающую сторону. Глубже: VTune (hotspots, PMU: IPC, cache misses), NSight (реплей кадра, occupancy). *(Л2 сл. 26–28)*

**Как ловить перф-регрессии?**

Тормоза копятся коммитами по 0,2 мс. Бюджеты на подсистемы — как контракт. Замер на каждый мерж: фиксированная сцена, headless-прогон, `tracy-capture`/`tracy-csvexport` в CI, перф-гейт — PR не проходит, если зона выросла больше чем на X%. *(Л2 сл. 29–30)*

### Лекция 3 · Job system

**Fork-join против графа задач.** Задачи A (0–40), B (0–70), C (0–55); D (длительность 30) зависит только от A.
- **Fork-join (Wait):** Wait ждёт весь workload — A, B и C. D стартует в 70, заканчивается в 100: лишний барьер — 30 единиц простоя.
- **Task graph:** зависимость — ребро графа A → D. D стартует сразу после A (в 40), заканчивается в 70. Так UE TaskGraph: `FGraphEvent` и prerequisites.

Главное из лекции:
- **Поток на подсистему** не масштабируется: потоков фиксированное число, баланс статичен, времена складываются в цепочку.
- **Пул:** workers = ядра − 1, очередь на поток, work stealing (Blumofe & Leiserson).
- **Wait помогает:** ждущий поток сам выполняет задачи.
- **Wicked:** context = атомарный счётчик, пулы High/Low/Streaming, блоки по 256, `alignas(64)`.
- **TLOU Remastered:** «jobify everything» на fibers — 132 → 55 → 33 мс.

**Процесс, поток, context switch, fibers**

Процесс: PID, права, виртуальная память, окружение, дескрипторы, рабочая папка, потоки. Поток: TID, стек, регистры (IP), TLS. Состояния потока: Running, Runnable, Blocked; переход = context switch через ядро (дорого). Fibers — потоки уровня пользователя, ядро о них не знает; корутина — fiber, который уступает другой и продолжает с места остановки. Вытесняющая многозадачность — ОС снимает поток по таймеру. *(Л3 сл. 5–9)*

**Кейс TLOU Remastered**

Порт на PS4 под 60 FPS: «jobify everything» на fiber-based job system — кадр 132 → 55 → 33 мс. Мораль: ядер 8–16, а главный поток один; без архитектуры железо простаивает. *(Л3 сл. 11)*

**Два класса работы в мягком реальном времени**

В бюджет кадра (ввод, симуляция, submit) — **параллелим**, чтобы уложиться в 16,6 мс. Фоном (загрузка, стриминг, декод текстур и звука, телеметрия) — **выносим** с главного потока. Job system обслуживает оба. У нас: анимация толпы — первый класс, загрузка и проверка скриптов — второй. *(Л3 сл. 12)*

**Почему «поток на подсистему» не масштабируется?**

Потоков фиксированное число, а ядер сколько угодно; баланс зашит статично, а нагрузка меняется каждый кадр; подсистемы связаны данными — времена складываются в цепочку. Альтернатива — двойная буферизация, но она добавляет ожидание. *(Л3 сл. 15)*

**Инверсия: task + пул + очередь**

Task — функция + данные без привязки к потоку. Потоков столько, сколько ядер (workers = cores − 1, живут всю сессию), главный поток — тоже участник. Планировщик — ваш код. Очередь потокобезопасна, `condition_variable` будит спящих: producer-consumer. *(Л3 сл. 16–17)*

**Work stealing**

Общая очередь — contention на каждой операции. Очередь на поток: кладём и берём локально; свободный worker крадёт из чужой (случайно или по кругу) — балансировка сама собой. Алгоритм Blumofe & Leiserson; так же в Wicked и в enkiTS. *(Л3 сл. 18)*

**Почему Wait — это не сон?**

Ждущий поток будит спящих, сам выполняет задачи из очереди и засыпает, только когда работы нет. Поэтому вложенный Wait не блокирует пул: задача ждёт подзадачи, не отнимая поток. *(Л3 сл. 19)*

**API job system WickedEngine и почему context, а не future**

`Initialize`, `ShutDown`, `GetThreadCount`, `Execute`, `Dispatch`, `IsBusy`, `Wait`. `context` — атомарный счётчик + приоритет на стеке; `future` — shared state и аллокация на каждый вызов. `std::async` плох: непрозрачный пул (или новый поток на вызов), нет лимита, приоритетов и единого шатдауна. *(Л3 сл. 20–21)*

**Пулы приоритетов в Wicked**

High — cores − 1 потоков, Low — cores − 2 (минус главный и streaming), Streaming — 1 поток для ресурсов. Приоритет вшит и в размеры пулов, и в планировщик ОС. *(Л3 сл. 22)*

**Детали JobQueue и Job в Wicked**

Блоки по 256 задач из `BlockAllocator` — без malloc на задачу. Атомарный `cnt`: пустую очередь видно без мьютекса — stealing не упирается в чужой лок. `alignas(64) Job` — своя кэш-линия против false sharing. Fixed-size function на 96 байт — вся задача в 128 байт, без скрытых аллокаций. Каждый worker прибит к ядру (affinity), streaming — на последнее. *(Л3 сл. 23, 25)*

**Dispatch — семантика compute-шейдера**

`jobCount`/`groupSize`: мелкие задачи пакуются в группы. `JobArgs`: `jobIndex` (как SV_DispatchThreadID), `groupID`, `groupIndex`, first/last в группе, `sharedmemory` — alloca на стеке в пределах группы. Штатно для параллельного апдейта ECS: «снапшот → диапазоны». *(Л3 сл. 24, 26)*

**Предел fork-join и task graph**

Wait ждёт весь workload, а не то, что нужно следующему шагу: D зависит только от A, но ждёт A, B и C — лишние барьеры. Task graph: зависимости — рёбра. UE TaskGraph: `FGraphEvent` — хэндл задачи, prerequisites — входы; named threads Game/Render/RHI — у API и игрового состояния есть владельцы; тики акторов — узлы `FTickFunctionTask`. *(Л3 сл. 28–30)*

**Как делить состояние game и render потоков?**

Снапшот / двойной буфер (render читает кадр N−1, game пишет N); отдельные graphic-объекты — копия состояния в начале кадра (в UE `FPrimitiveSceneProxy`); очередь команд — game отдаёт render'у команды с данными, а не доступ. *(Л3 сл. 31)*

**Корректность у Bungie и fibers у Naughty Dog**

Bungie: каждая job декларирует reads/writes через policy builder, доступ без прав — assert; по графу находят потенциально параллельные jobs и ловят конфликт до исполнения; stress lab — 150 PC и 600 консолей. Naughty Dog: счётчики вместо future; задача ждёт счётчик → fiber уходит в очередь ожидания, worker берёт следующую задачу — ожидание не держит поток. *(Л3 сл. 32–33)*

### Лекция 4 · Архитектура рендера

Слои рендера:
1. **High-level** — scene graph, culling, ECS.
2. **Render graph** — проходы, барьеры, transient-ресурсы.
3. **RHI** — device, command list, PSO, binding layout.
4. **Драйвер · ОС.**

Implicit API (OpenGL, DX11) — драйвер решает всё; explicit (Vulkan, DX12) — барьеры, память и синхронизация руками. Наш движок — OpenGL 3.3; похожую на RHI роль играет `IRenderAdapter`.

ЛР 3 — варианты:
- **А · Render graph:** setup (reads/writes) + execute, автобарьеры, culling проходов, aliasing.
- **Б · Рефлексия шейдеров:** DXIL/SPIR-V/`glGetProgramInterface` + живой потребитель.
- **В · RHI-слой:** свой или nvrhi/bgfx/sokol.
- **Г · Материалы:** материал как данные, инстансы, кэш PSO.
- **Д · Culling + batching:** visible set как данные, submission отдельной фазой.

**Implicit и explicit графические API**

Implicit (OpenGL, DX11): драйвер решает всё — скрытая память, «магическая» синхронизация, высокий оверхед CPU. Explicit (Vulkan, DX12): ручной контроль, свои барьеры, кучи памяти, предсказуемость. Цена: огромный бойлерплейт, гонки RAW/WAW, свои аллокаторы VRAM. Ответ архитектуры — слои RHI и Render Graph. Наш движок — OpenGL 3.3 (implicit). *(Л4 сл. 4–6)*

**Проблема классического рендера (Zenith Engine)**

CPU stall в render thread: поток отрисовки ждёт создания буферов и текстур через прямые вызовы API. До первого кадра создаются 532 текстуры (45 МБ RAM) и 2713 буферов (20 МБ RAM). Решение: создавать ресурсы в потоках загрузки, сразу освобождать RAM, делить компиляцию PSO на ядра — render thread только рисует. *(Л4 сл. 5, 13)*

**Зачем RHI и его базовые интерфейсы**

Тонкая C++-абстракция над API, без `#ifdef VULKAN` в high-level коде, единые структуры для пайплайнов, дескрипторов, команд. Интерфейсы: `IRenderDevice` (фабрика ресурсов), `ICommandList` (запись команд), `IPipelineState` (монолитное состояние конвейера), `IBindingLayout`/`IBindingSet` (чертёж слотов и их заполнение). Готовые: NVRHI, Diligent, Wicked. У нас похожую роль играет `IRenderAdapter`. *(Л4 сл. 7–8, 14)*

**DX12 vs Vulkan — соответствие понятий**

Layout привязки: Root Signature ↔ Pipeline Layout. Дескрипторы: Descriptor Heaps ↔ Descriptor Pools/Sets. Очередь: `ID3D12CommandQueue` ↔ `VkQueue`. Барьеры: Resource Barriers ↔ Pipeline Barriers. *(Л4 сл. 9)*

**Особенности Metal и Vulkan**

**Metal:** `MTLCommandQueue` потокобезопасна; `MTLHeap`; перед уходом iOS в фон — `waitUntilScheduled` (иначе jetsam). **Vulkan:** буферы просты (VMA потокобезопасна); текстура — три шага: барьер в TRANSFER_DST → `vkCmdCopyBufferToImage` → барьер в SHADER_READ_ONLY; `VkQueue` не потокобезопасна, отдельная transfer-очередь есть не везде. *(Л4 сл. 10–11)*

**Многопоточная запись команд**

Главная фича новых API: у каждого worker свой CommandAllocator/CommandPool, `ICommandList` пишутся параллельно, главный поток только сабмитит готовые списки в правильном порядке. Без этого переход на DX12/Vulkan не имеет смысла. *(Л4 сл. 12)*

**Материал как данные**

Боль: шейдер и текстура захардкожены в точке отрисовки, константы по одной через `glGetUniformLocation`, новый материал = перекомпиляция. Инверсия: шейдер, текстуры, константы и состояния (блендинг, depth-test) — в активе; одна точка `bind material → draw`; битый материал — ошибка при загрузке. Инстансы переопределяют параметры базового (UE material instances); PSO собирается по ключу и кэшируется. Примеры форматов: glTF, Unity .mat, Godot, Filament. *(Л4 сл. 15–21, 25)*

**Culling и submission**

Боль: RenderSystem рисует каждую сущность сразу, без отсева, порядок зашит в цикле. Решение: frustum test → **visible set как данные** (можно сортировать, батчить, считать, отдавать задачам) → submission отдельной фазой: сортировка по шейдеру/материалу/PSO (ключ PSO = ключ сортировки) и подача пакетом. *(Л4 сл. 22–24)*

**Рефлексия шейдеров**

Два источника правды: слоты в C++ и биндинги в шейдере — ручная синхронизация ломается (мусор на экране). Механика: DX12 — `ID3D12ShaderReflection`/DXC (Name, Type, BindPoint, Space), SPIR-V — spirv-cross, OpenGL — `glGetProgramInterface`. Потребители: автогенерация root signature, маппинг «имя → слот» (`SetTexture("albedo")`), vertex input layout. Шейдер — единая точка правды. *(Л4 сл. 26–30)*

**Render graph**

Боль — «лестница» ручных барьеров, новый проход = перестройка. Декларативно: setup-фаза (проход объявляет reads/writes через builder, без GPU-работы) и execute-фаза (лямбда с командами). Граф сам выводит порядок, барьеры (в GL — только зависимости и порядок), отбрасывает проходы, чей результат никто не читает, знает lifetime ресурсов → transient-пул и aliasing памяти. Плюс параллельная запись и async compute. Цена — setup-фаза; на 3–5 проходах это задел, а не выигрыш. *(Л4 сл. 31–36)*

**ЛР 3: варианты**

А — render graph; Б — рефлексия шейдеров с живым потребителем; В — RHI-слой (свой или nvrhi/bgfx/sokol); Г — материалы как данные, инстансы, кэш PSO; Д — culling + batching с замерами до/после. *(Л4 сл. 38)*

## 19. Lua для C++-программиста

**Что пишется иначе и где легко ошибиться.**

### C++ → Lua

| C++ | Lua | Заметка |
|---|---|---|
| `// комментарий` | `-- комментарий`, `--[[ … ]]` | |
| `int x = 1;` | `local x = 1` | без `local` — глобальная переменная |
| `a != b` | `a ~= b` | |
| `a && b \|\| !c` | `a and b or not c` | ложны только `nil` и `false`; 0 и "" — истина |
| `x += 1; x++;` | `x = x + 1` | нет `+=` и `++` |
| `c ? a : b` | `c and a or b` | врёт, если `a` = false или nil |
| `nullptr` | `nil` | |
| `"a" + std::to_string(n)` | `"a" .. n` | склейка строк — `..` |
| `if (c) { … } else if (d) { … }` | `if c then … elseif d then … end` | |
| `for (int i = 0; i < n; ++i)` | `for i = 1, n do … end` | границы включительно, индексы с 1 |
| `for (auto& e : list)` | `for _, e in ipairs(list) do … end` | `pairs` — для словаря, порядок не гарантирован |
| `continue;` | `goto continue` … `::continue::` | continue нет |
| `std::vector`, `std::map` | `{}` — таблица | `#t` — длина массива |
| `obj->method(x)` | `obj:method(x)` | = `obj.method(obj, x)` |
| `7 / 2` (int) | `7 // 2` | `/` в Lua всегда даёт float: 3.5 |
| `throw` / `try` | `error("…")` / `pcall(f)` | `assert(cond, "msg")` тоже бросает |

### Основы и числа

```lua
local speed = 1.5           -- local!
count = 0                   -- глобальная
local a, b = 1, 2
a, b = b, a                 -- обмен
print(type(nil), type(true), type(1), type("s"), type({}))
-- nil  boolean  number  string  table

if speed and speed > 0 then print("едем") end
local name = cfg.name or "Core"   -- значение по умолчанию
```

```lua
math.type(1)     --> "integer"
math.type(1.0)   --> "float"
1 == 1.0         --> true
7 / 2            --> 3.5   (/ всегда float)
7 // 2           --> 3     (деление вниз)
2 ^ 10           --> 1024.0
math.floor(3.7)  --> 3
math.tointeger(3.0) --> 3
string.format("%d", 3.0)   --> "3"; 3.5 — ошибка
```

### Таблицы и циклы

```lua
local list = {"a", "b", "c"}   -- индексы с 1!
list[#list + 1] = "d"           -- в конец
table.insert(list, "e")
table.remove(list, 1)
local cfg = {speed = 1.5, tag = "Core"}
print(cfg.speed, cfg["tag"])    -- одно и то же
cfg.tag = nil                   -- удалить ключ
print(table.concat({1, 2, 3}, ", "))
```

```lua
for i = 1, 10 do end            -- 1..10
for i = 10, 1, -2 do end        -- шаг
for i, v in ipairs(list) do end -- массив по порядку
for k, v in pairs(cfg) do end   -- все ключи
while t > 0 do t = t - dt end
repeat n = n + 1 until n >= 3
for i = 1, 5 do
  if i == 3 then goto continue end
  ::continue::
end
```

### Функции и методы

```lua
local function dist(a, b)
  local dx, dy = b.x - a.x, b.y - a.y
  return math.sqrt(dx * dx + dy * dy), dx, dy
end
local d, dx, dy = dist(p, q)   -- несколько значений

local function counter()        -- замыкание
  local n = 0
  return function() n = n + 1; return n end
end

local function sum(...)          -- varargs
  local s = 0
  for _, v in ipairs({...}) do s = s + v end
  return s
end
```

```lua
obj:move(1)        -- = obj.move(obj, 1)
function Enemy:on_update(dt)    -- = Enemy.on_update = function(self, dt)
  local p = self.entity:get_position()   -- у usertype всегда «:»
end

-- ошибка: метод не получит объект
local p = self.entity.get_position()
```

### Метатаблицы

```lua
local Vec = {}
Vec.__index = Vec                 -- методы ищутся в Vec
function Vec.new(x, y)
  return setmetatable({x = x, y = y}, Vec)
end
function Vec:len() return math.sqrt(self.x ^ 2 + self.y ^ 2) end
Vec.__add = function(a, b) return Vec.new(a.x + b.x, a.y + b.y) end
Vec.__tostring = function(v) return "(" .. v.x .. ", " .. v.y .. ")" end
print(Vec.new(3, 4):len(), tostring(Vec.new(1, 2) + Vec.new(1, 1)))
```

Метаметоды: `__index`, `__newindex`, `__call`, `__add`, `__eq`, `__lt`, `__le`, `__len`, `__concat`, `__tostring`, `__gc`, `__close`. Так устроен наш `uzlezz.behaviour`: класс — таблица с метатаблицей `{__index = uzlezz.ScriptBehaviour}`, экземпляр — таблица с метатаблицей `{__index = класс}` (см. [раздел 5](#где-ищется-метод-цепочка-метатаблиц)).

### Наш API

```lua
---@class Mover : uzlezz.Behaviour
Mover = uzlezz.behaviour { fields = { speed = 1.0, target = "Core" } }
function Mover:on_create() self.timer = 0.0 end
function Mover:on_update(dt)
  local p = self.entity:get_position()
  p.x = p.x + self.entity:get_field("speed") * dt
  self.entity:set_position(p)
end
function Mover:on_destroy() end
function Mover:on_reload(old) self.timer = old.timer end  -- необязательно
```

- **Entity:** `is_alive`, `id`, `get_position`, `set_position`, `set_yaw`, `get_tag`, `set_animation_speed`, `get_field`, `set_field`.
- **World:** `input_pressed`, `find_by_tag`, `find_all_by_tag`, `spawn_prefab`, `destroy`, `set_status`.
- **Есть:** base (print, pairs, ipairs, type, tostring, tonumber, pcall, error, assert, setmetatable, select), `math`, `table`, `string`.
- **Нет:** `io`, `os`, `require`, `debug`, `coroutine`, `utf8`, `dofile`, `loadfile`.

### Найди ошибку

**`if hp != 0 then … end`**

В Lua «не равно» — `~=`. `!=` — синтаксическая ошибка.

**`for i = 0, #list do print(list[i]) end`**

Индексы с 1: `list[0]` — nil. Правильно `for i = 1, #list do` или `ipairs`.

**`core:set_field("health", core:get_field("health") / 2)`**

`/` всегда даёт float: 9 / 2 = 4.5 — в целое поле не запишется. Нужно `// 2`.

**`function Enemy:on_update(dt) speed = 3 end`**

Без `local` `speed` — глобальная: общая на все экземпляры и переживает reload. Состояние экземпляра — в `self.speed`.

**`if count then … end` при count = 0**

0 в Lua — истина; ложны только `nil` и `false`. Нужно `if count > 0 then`.

**`local p = self.entity.get_position()`**

Метод usertype вызывается через двоеточие: `self.entity:get_position()`. Через точку не передаётся сам объект.

## 20. Шпаргалка: цифры и где что лежит

| Что | Значение |
|---|---|
| Версии | Lua 5.4.8 · sol2 3.3.1 · C++17 |
| Опрос вотчера / debounce | 0,2 с / два опроса подряд |
| Бюджет вызова скрипта | 100 мс · хук каждые 1000 инструкций |
| Потолок кучи Lua | 256 МБ |
| Шаг GC в кадре | `LUA_GCSTEP, 32` |
| Core | 10 HP · импульс r = 6 · 2 с |
| Enemy | 1,5 м/с · урон 1 · атака r = 1,2 |
| Waves | 4 + 2 за волну · 0,6 с · r = 10 |
| Тесты | 10 наборов ctest · 30 мин симуляции · 100 Stop/Play · 200 reload |
| Клавиши | Space — волна · F — импульс · ⌘P — Play |

| Файл | Что там |
|---|---|
| `src/scripting/ScriptSystem.h/.cpp` | state, LuaBehaviour, bindEngine, reload, hotReload, update, Sandbox |
| `src/scripting/ScriptWatcher.h/.cpp` | вотчер на job system, проверка синтаксиса |
| `src/scripting/ScriptComponent.h` | путь, класс, префаб, поля |
| `src/prefabs/PrefabManager.cpp` | spawn из JSON, откат, Save to Prefab |
| `src/editor/EditorContext.cpp` | владелец ScriptSystem, Play/Stop, применение reload |
| `src/editor/panels/GameplayPanel.cpp` | Open Arena, Reload, режим L1/L2, куча и время |
| `src/editor/panels/InspectorPanel.cpp` | карточка Script, Save to Prefab |
| `assets/scripts/*.lua` | механика: waves, enemy, core |
| `assets/prefabs/*.json` | arena, core, waves, enemy |
| `tools/lua-stubs/uzlezz.lua`, `.luarc.json` | подсказки в IDE |
| `tests/ScriptSystemTests.cpp`, `ScriptRuntimeTests.cpp` | механика, ошибки, лимиты, reload |
| `cmake/LuaRuntime.cmake`, `tools/setup_lua.sh` | Lua внутри exe, закреплённые исходники |
| `docs/lab2/README.md`, `DEMO.md` | сборка, архитектура, сценарий демо |
