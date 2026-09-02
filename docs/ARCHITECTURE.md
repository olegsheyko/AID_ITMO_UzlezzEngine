# Uzlezz Engine — архитектура

Схемы сгенерированы по коду ревизии `756bd6b` (ПЗ_5) и сверены с исходниками вручную.
Каждая диаграмма отражает то, что реально вызывается в текущей сборке.

- **Стек:** C++17, OpenGL 3.3 core, GLFW, GLAD, Assimp, stb_image, nlohmann/json, Dear ImGui + ImGuizmo, CMake
- **Объём:** ~6.8 тыс. строк в `src/`, 66 файлов, 8 внешних библиотек в `external/`
- **Точка входа:** [`src/main.cpp`](../src/main.cpp)

---

## 1. Модули и направление зависимостей

```mermaid
flowchart TD
    main["main.cpp<br/>точка входа"]

    subgraph core["core/"]
        App["Application<br/>главный цикл, dt"]
        SM["StateManager<br/>stack&lt;IGameState&gt;"]
        Log["Logger (singleton)"]
        SL["ServiceLocator<br/>-&gt; EventDispatcher"]
    end

    subgraph states["states/"]
        Loading["LoadingState"]
        Editor["EditorState<br/>1489 строк, ImGui"]
        Menu["MenuState"]
        Gameplay["GameplayState"]
    end

    subgraph ecs["ecs/"]
        World["World<br/>сущности + хранилища"]
        Sys["PhysicsSystem<br/>CameraSystem<br/>SpinSystem<br/>RenderSystem<br/>DebugRenderSystem"]
    end

    subgraph render["render/"]
        IRA["IRenderAdapter<br/>интерфейс"]
        GL["OpenGLRenderAdapter<br/>glad + glfw"]
    end

    subgraph res["resources/"]
        RM["ResourceManager<br/>singleton + кэш"]
        Loaders["MeshLoader / TextureLoader<br/>ShaderLoader / SceneManifest"]
        HR["HotReload (singleton)"]
    end

    subgraph inp["input/"]
        IM["InputManager<br/>singleton, action map"]
        IIH["IInputHandler"]
        GIH["GlfwInputHandler"]
    end

    Ext["external/: GLFW · GLAD · Assimp<br/>stb_image · nlohmann/json<br/>Dear ImGui · ImGuizmo"]

    main --> App
    App --> SM
    App --> IRA
    App --> RM
    App --> IM
    App --> HR
    SM --> states
    Editor --> World
    Editor --> Sys
    Gameplay --> World
    Gameplay --> Sys
    Sys --> World
    Sys --> IRA
    Sys --> IM
    RM --> Loaders
    Loaders --> IRA
    IRA -.->|реализует| GL
    IIH -.->|реализует| GIH
    IM --> IIH
    GL --> Ext
    Loaders --> Ext
    Editor --> Ext

    App -.->|"dynamic_cast к OpenGLRenderAdapter:<br/>абстракция протекает"| GL
```

Единственный шов между движком и OpenGL — `IRenderAdapter`. Он держится всюду, кроме одного места:
`Application::init` делает `dynamic_cast<OpenGLRenderAdapter*>`, чтобы получить `GLFWwindow*` для ImGui
и для `GlfwInputHandler` ([`Application.cpp:26`](../src/core/Application.cpp), [`:118`](../src/core/Application.cpp)).

---

## 2. Инициализация

```mermaid
sequenceDiagram
    autonumber
    participant m as main()
    participant a as Application
    participant r as OpenGLRenderAdapter
    participant i as InputManager
    participant g as ImGui / ImGuizmo
    participant rm as ResourceManager
    participant sm as StateManager

    m->>m: Logger::openFile("engine.log")
    m->>a: init(800, 600, "Uzlezz Engine")
    a->>r: init(w, h, title)
    r->>r: glfwInit()
    r->>r: window hints: GL 3.3 core, maximized
    r->>r: glfwCreateWindow + makeContextCurrent
    r->>r: gladLoadGLLoader()
    r->>r: glfwSwapInterval(1) — vsync
    r->>r: glEnable(DEPTH_TEST, BLEND)
    r->>r: createRenderResources():<br/>шейдер vertex/fragment.glsl + VAO примитивов
    r-->>a: true
    a->>i: initialize(GlfwInputHandler(window))
    a->>g: CreateContext, docking, ImplGlfw + ImplOpenGL3
    a->>rm: init(renderer)
    a->>sm: push(LoadingState)
    a->>a: lastFrameTime_ = Clock::now()
    a-->>m: true
    m->>a: run()
```

Порядок важен: контекст OpenGL создаётся первым, поэтому все дальнейшие загрузки ресурсов
могут сразу заливать данные на GPU. `ResourceManager` получает адаптер как «сырой» указатель
и хранит его до конца жизни процесса.

---

## 3. Один кадр — кто кого дёргает

```mermaid
sequenceDiagram
    autonumber
    participant a as Application::run
    participant r as RenderAdapter
    participant i as InputManager
    participant hr as HotReload
    participant s as EditorState
    participant w as World
    participant sysu as Physics/Spin/Camera
    participant sysr as Render/DebugRender

    loop while renderer.isRunning()
        a->>a: dt = now - lastFrame, clamp до 0.1 c
        a->>r: pollEvents() — glfwPollEvents
        a->>i: updateState() — опрос ~25 клавиш,<br/>prev/current, дельта мыши

        rect rgb(245, 246, 250)
        note right of a: update(dt)
        a->>hr: update() — stat() по всем watched-файлам
        hr-->>a: список изменённых
        a->>a: ResourceManager::reloadShadersForFile(...)
        a->>s: current()->update(dt)
        s->>s: счётчик FPS
        opt режим Play
            s->>sysu: physics / spin / gameCamera напрямую
            sysu->>w: forEach<Transform, Rigidbody> ...
        end
        end

        rect rgb(245, 246, 250)
        note right of a: render()
        a->>r: beginFrame(0.1, 0.1, 0.2) — очистка бэкбуфера
        a->>a: ImGui NewFrame + ImGuizmo::BeginFrame
        a->>s: current()->render()
        s->>s: DockSpace, MainMenu, Toolbar,<br/>Hierarchy, Inspector, Statistics
        s->>s: Viewport: editorCamera_.update(...) — да, апдейт внутри рендера
        s->>r: beginViewportFrame(w, h) — переключение на FBO
        s->>sysr: renderSystem_.render(world_)
        sysr->>w: forEach<Transform, MeshRenderer>
        s->>sysr: debugRenderSystem_.render(world_) — AABB по F3
        s->>r: endViewportFrame() — назад на бэкбуфер
        s->>s: ImGui::Image(texture) — сцена как текстура в панели
        a->>a: ImGui::Render + ImplOpenGL3_RenderDrawData
        a->>r: endFrame() — glfwSwapBuffers
        end
    end
```

Ключевая особенность: сцена рисуется **не в бэкбуфер, а в отдельный FBO**, и уже потом
попадает в кадр как текстура внутри ImGui-панели `Viewport`
([`EditorState.cpp:1239`](../src/states/EditorState.cpp), [`OpenGLRenderAdapter.cpp:96`](../src/render/OpenGLRenderAdapter.cpp)).

---

## 4. Состояния

```mermaid
stateDiagram-v2
    direction LR
    [*] --> LoadingState : push в Application init
    LoadingState --> EditorState : таймер 2 c истёк<br/>Application.cpp стр. 80

    state "MenuState" as Menu
    state "GameplayState" as Play
    Menu --> Play : Enter<br/>Application.cpp стр. 84

    note right of Menu
        Недостижимы в текущей сборке:
        MenuState никто не push-ит.
        Живой код от ПЗ 1-4, но кадр
        через них уже не проходит.
    end note
```

Переходы жёстко прописаны в `Application::update` через `dynamic_cast` к конкретному типу
состояния — сами состояния запросить переход не могут.

---

## 5. ECS: модель данных

```mermaid
classDiagram
    direction LR

    class World {
        -unordered_set~Entity~ aliveEntities_
        -unordered_map~type_index,IStorage~ storages_
        -vector~UpdateSystem*~ updateSystems_
        -vector~RenderSystemBase*~ renderSystems_
        +createEntity() Entity
        +destroyEntity(Entity)
        +addComponent~T~(e) T&
        +getComponent~T~(e) T&
        +hasComponent~T~(e) bool
        +removeComponent~T~(e)
        +forEach~First,Rest~(func)
        +updateSystems(dt)
        +renderSystems()
    }

    class IStorage {
        <<interface>>
        +remove(Entity)
    }

    class Storage {
        +unordered_map~Entity,T~ components
        +remove(Entity)
    }

    class System {
        <<interface>>
    }
    class UpdateSystem {
        <<interface>>
        +update(World&, float dt)
    }
    class RenderSystemBase {
        <<interface>>
        +render(World&)
    }

    class PhysicsSystem {
        -float gravityStrength_
        -size_t lastCollisionCount_
    }
    class CameraSystem {
        -IRenderAdapter& renderer_
    }
    class SpinSystem
    class RenderSystem {
        -IRenderAdapter& renderer_
        -size_t lastDrawnMeshCount_
    }
    class DebugRenderSystem {
        -IRenderAdapter& renderer_
        -bool enabled_
    }

    World o-- IStorage : один на тип компонента
    IStorage <|-- Storage
    System <|-- UpdateSystem
    System <|-- RenderSystemBase
    UpdateSystem <|-- PhysicsSystem
    UpdateSystem <|-- CameraSystem
    UpdateSystem <|-- SpinSystem
    RenderSystemBase <|-- RenderSystem
    RenderSystemBase <|-- DebugRenderSystem
    World ..> UpdateSystem : updateSystems
    World ..> RenderSystemBase : renderSystems
```

`Entity` — это просто `uint32_t`, наследования у игровых объектов нет вообще.
Компоненты — чистые структуры без методов, поведение целиком в системах:

| Компонент | Поля | Кто читает |
|---|---|---|
| `Transform` | `position`, `rotation` (углы Эйлера), `scale` | все системы |
| `MeshRenderer` | 8 строковых id + 8 `shared_ptr` на кэшированные ресурсы | `RenderSystem` |
| `Rigidbody` | `velocity`, `acceleration`, `mass`, `useGravity` | `PhysicsSystem` |
| `Collider` | `type` (Box/Sphere), `halfExtents`, `offset`, `radius` | `PhysicsSystem`, `DebugRenderSystem` |
| `Camera` | `fov`, `nearClip`, `farClip`, `aspectRatio`, `active`, `viewMatrix`, `projectionMatrix` | `CameraSystem`, `RenderSystem` |
| `Hierarchy` | `parent`, `children` | `RenderSystem`, `World::destroyEntity` |
| `Tag` | `name` | редактор, логи |
| `Spin` | `speed` | `SpinSystem` |

Допуск типа в ECS даёт `IsComponent<T>` + `static_assert`: контракт проверяется на этапе компиляции,
а не через наследование от базового класса.

**Как работает `forEach<A, B>`** — двухуровневый хеш-поиск:

```mermaid
flowchart LR
    F["forEach&lt;Transform, MeshRenderer&gt;"] --> S1["Storage&lt;Transform&gt;<br/>unordered_map&lt;Entity, Transform&gt;"]
    S1 -->|"итерация в порядке хеш-таблицы"| E["каждая пара (entity, Transform&)"]
    E -->|"hasComponent&lt;MeshRenderer&gt;(entity)"| S2["Storage&lt;MeshRenderer&gt;<br/>ещё один хеш-поиск"]
    S2 -->|"есть"| CB["вызов колбэка"]
    S2 -->|"нет"| SKIP["пропуск"]
```

Ни архетипов, ни SoA, ни плотных массивов: порядок обхода — это порядок бакетов
`unordered_map`, а на каждый дополнительный тип компонента приходится отдельный хеш-поиск.

---

## 6. Ресурсы: загрузка и владение

```mermaid
flowchart TD
    JSON["assets/scenes/demo_scene.json<br/>meshes / textures / shaders / entities"]
    SMF["SceneManifest::loadFromFile<br/>nlohmann/json"]
    ST["EditorState / GameplayState<br/>createSceneFromManifest()"]
    RM{"ResourceManager::load&lt;T&gt;(path)<br/>есть в кэше?"}
    CACHE["unordered_map&lt;path, shared_ptr&lt;Resource&lt;T&gt;&gt;&gt;<br/>meshCache_ / textureCache_ / shaderCache_"]

    ML["MeshLoader — Assimp<br/>Triangulate, GenNormals,<br/>CalcTangentSpace, JoinIdenticalVertices"]
    TL["TextureLoader<br/>.dds -> свой DXT1/DXT5 декодер<br/>иначе stb_image"]
    SHL["ShaderLoader<br/>ifstream -> строка"]

    ADP["IRenderAdapter<br/>uploadMesh / createTexture / createShaderProgram"]
    GPU[("GPU: VAO / VBO / EBO,<br/>texture id, program id")]
    COMP["MeshRenderer (компонент)<br/>держит те же shared_ptr"]
    HR["HotReload — stat() каждый кадр<br/>-> reloadShadersForFile"]

    JSON --> SMF --> ST --> RM
    RM -->|"попадание"| CACHE
    RM -->|"промах"| ML
    RM -->|"промах"| TL
    RM -->|"промах"| SHL
    ML --> ADP
    TL --> ADP
    SHL --> ADP
    ADP --> GPU
    ML --> CACHE
    TL --> CACHE
    SHL --> CACHE
    CACHE --> COMP
    HR -.->|"только шейдеры"| SHL
    TL -.->|"free(pixels) сразу после заливки"| TL

    ADP -.->|"clearCache() при shutdown:<br/>destroyMesh/Texture/ShaderProgram"| GPU
```

Всё синхронно, в главном потоке, во время `onEnter()` состояния. Владение двойное:
`shared_ptr` лежит и в кэше `ResourceManager`, и в компоненте `MeshRenderer` — то есть кэш
никогда не отдаёт память, пока жив процесс. GPU-хендл (`vao`, `textureId`, `programId`)
хранится **внутри** CPU-структуры данных ресурса, поэтому «данные» и «ресурс GPU» —
это один и тот же объект.

---

## 7. Отрисовка одного меша

```mermaid
flowchart TD
    R["RenderSystem::render(world)"] --> FE["forEach&lt;Transform, MeshRenderer&gt;"]
    FE --> CHK{"cachedMesh и cachedShader<br/>загружены?"}
    CHK -->|"нет"| SKIP["пропуск"]
    CHK -->|"да"| WM["buildWorldMatrix — рекурсия по Hierarchy,<br/>новый unordered_set на каждую сущность"]
    WM --> USE["useShaderProgram(programId)"]
    USE --> MTX["setupMatrices: model + view + projection"]
    MTX --> CAM["поиск активной камеры:<br/>forEach&lt;Transform, Camera&gt; внутри вызова<br/>-> O(N x M) за кадр"]
    MTX --> LIGHT["setupLighting: направленный свет,<br/>значения захардкожены в коде"]
    LIGHT --> SUB{"есть subMeshes?"}
    SUB -->|"да"| DRAW1["для каждого submesh:<br/>bind diffuse в слот 0 -> drawIndexed"]
    SUB -->|"нет"| DRAW2["bind baseColor -> drawIndexed"]
    DRAW1 --> RESET["useShaderProgram(0)<br/>bindTexture2D(0, 0)"]
    DRAW2 --> RESET

    UNUSED["normal / metallic / roughness<br/>AO / height — поля есть в Material<br/>и в MeshRenderer, но в слоты не биндятся"]
    DRAW1 -.-> UNUSED
```

Второй, независимый путь отрисовки — `drawPrimitive` / `drawDebugAABB` внутри самого адаптера:
у него свой `ShaderProgram shader_` (`assets/shaders/vertex.glsl` + `fragment.glsl`) и свои VAO
примитивов, созданные в `createRenderResources()`. Им пользуется только `DebugRenderSystem`.

---

## 8. Многопоточность

Её нет. `grep -r "std::thread|std::mutex|std::async|std::future|std::atomic" src/` не даёт ни одного
совпадения. Весь кадр — загрузка ресурсов, физика, обход ECS, вызовы OpenGL, ImGui — выполняется
в одном потоке. Синхронизация не нужна, потому что нечего синхронизировать.
