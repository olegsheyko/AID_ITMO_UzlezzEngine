# ЛР 3: RenderDoc и Tracy, как показать преподавателю

Допфича «Рендер разметка RenderDoc + Tracy» (2 балла) и обязательный RenderDoc-захват «после» для GL-команд.
Показ на Windows-ноутбуке: на macOS RenderDoc не работает с OpenGL, а GPU-зоны Tracy для OpenGL на Apple отключены самим Tracy.

## 1. Что сделано в коде

**Разметка кадра для RenderDoc.** Кадр разбит на именованные группы команд. В RenderDoc они видны как папки в Event Browser:

- **Scene View**: Sky, затем **Submission packet: N objects** (весь пакет), внутри **Group k: меш | текстура** (по группе на каждый набор одинаковых шейдера, меша и текстуры), затем Grid и Selection outline;
- **Game View**: Sky, пакет, Debug colliders;
- **ImGui**: интерфейс редактора;
- **Culling benchmark frame**: кадр стенда замеров.

**Имена GPU-объектов.** Текстуры, меши (VAO) и шейдерные программы подписаны именами файлов: в RenderDoc вместо «Texture 37» видно `stone.dds`, вместо «VAO 12» `primitive:cube #0`, вместо «Program 3» `mesh_vertex.glsl | mesh_fragment_simple_texture.glsl`.

**Tracy.**

- CPU-зоны каждой фазы рендера: `Scene visibility and submission` → `Visible set and sorted packet` → `Visibility sync`, `Visibility query`, `Build and sort packet`; затем `Submission packet`.
- GPU-зоны (только Windows): строка **OpenGL** на таймлайне с теми же группами, что в RenderDoc, и временем выполнения на видеокарте.
- Графики: `Render: registered`, `visible`, `submitted`, `shader switches`, `texture switches`, `depth inversions`, `visibility updates`, `CPU prepare ms`, `CPU submit ms`.
- Сообщения на таймлайне при переключении галочек: `Frustum culling ON/OFF`, `Sort submission ON/OFF`.

**Где в коде.**

- `src/render/GpuMarkers.h/.cpp`: загрузка функций разметки (KHR_debug, запасной путь EXT_debug_marker), GPU-зоны Tracy.
- `src/render/IRenderAdapter.h`: `pushDebugGroup`, `popDebugGroup`, `setDebugLabel`, `debugMarkersActive`, RAII-класс `RenderDebugScope`.
- `src/ecs/RenderSystem.cpp`: зоны фаз, графики, группы пакета.
- `src/editor/EditorContext.cpp`: группы Scene View и Game View; `src/core/Application.cpp`: группа ImGui.
- `src/resources/ResourceManager.cpp`: имена текстур, мешей, программ.
- `src/editor/panels/RendererInfoPanel.cpp`: сообщения Tracy при переключении.

Граница слоя не нарушена: рендер и редактор зовут только методы адаптера, прямые вызовы GL остались в `src/render/`.

## 2. Подготовка ноутбука (один раз, заранее)

1. Подтянуть код и собрать **Release** с Tracy (он включён по умолчанию, `ENGINE_ENABLE_TRACY=ON`):
   `git pull`, затем `cmake -S . -B build/windows -A x64` и `cmake --build build/windows --config Release`.
2. Проверить тесты: `ctest --test-dir build/windows -C Release --output-on-failure`.
3. Установить **RenderDoc** (renderdoc.org, версия 1.x) и иметь **Tracy 0.14.1** (`tracy-profiler.exe`); у однокурсника пути уже прописаны в `tools/defense/local.json`.
4. Запустить движок один раз и найти в логе строку **`OpenGLRenderAdapter: debug markers: KHR_debug`**. Если там `none`, разметки в RenderDoc не будет (см. раздел 7).

## 3. Захват в RenderDoc

1. Открыть RenderDoc, вкладка **Launch Application**.
2. **Executable Path**: `...\build\windows\Release\GameEngine.exe`.
3. **Working Directory**: папка, где лежит `assets` (папка с exe: сборка копирует туда `assets`). Если пусто, движок не найдёт сцену.
4. **Command-line Arguments**: `--editor-culling --no-vsync`.
5. Нажать **Launch**. В левом верхнем углу окна движка появится оверлей RenderDoc: «Captures: 0, F12/PrtScr to capture».
6. Дождаться загрузки сцены (в окне Scene есть «Scene 10000 / GPU N»), открыть окно **Renderer Info**.
7. **Захват «после»**: Frustum culling и Sort submission включены → нажать **F12** (или PrintScreen).
8. **Захват «до»**: снять обе галочки → **F12**. Затем включить обратно.
9. Снимки появятся в окне RenderDoc. Сохранить: правый клик по снимку → **Save** → например `docs/culling/renderdoc/after.rdc` и `before.rdc`. Это артефакт приёмки из ТЗ.

Окна Scene и Game в редакторе рисуются только когда видны; в снимок попадёт то, что открыто.

## 4. Что показать в RenderDoc (около 2 минут)

1. Открыть снимок «после» (двойной клик). Слева **Event Browser**: развернуть **Scene View** → **Submission packet: ~2000 objects**. Сказать: «это наш пакет, он подан одной фазой».
2. Развернуть пакет: внутри **Group 1, Group 2…** по сочетанию меша и текстуры, примерно 16–20 групп. Сказать: «сортировка собрала одинаковые ресурсы подряд, внутри группы объекты от ближних к дальним».
3. Кликнуть любой `glDrawElements` внутри группы:
   - вкладка **Texture Viewer**: что нарисовано к этому моменту (цель Scene View);
   - вкладка **Pipeline State** → Fragment Shader: текстура подписана именем файла, программа подписана именами шейдеров;
   - вкладка **Mesh Viewer**: вершины куба или персонажа, VAO подписан именем меша.
4. Открыть снимок «до»: **Submission packet: 10000 objects** и групп столько, сколько смен текстуры (тысячи). Сказать: «без отсева уходит всё, без сортировки ресурсы меняются почти на каждом объекте».
5. Для чисел: **Window → Statistics** показывает число draw-вызовов в каждом снимке.

## 5. Что показать в Tracy (около 1,5 минуты)

1. Запустить `tools/defense/02 - ДВИЖОК И TRACY - живой профиль.cmd` (или вручную: `GameEngine.exe --editor-culling --no-vsync` и в Tracy **Connect** к `127.0.0.1`).
2. На таймлайне главного потока раскрыть кадр: `Scene View` → `Scene visibility and submission` → `Visible set and sorted packet` (внутри `Visibility sync`, `Visibility query`, `Build and sort packet`) → `Submission packet`.
3. Ниже потоков строка **OpenGL**: GPU-зоны с теми же именами, что в RenderDoc (`Scene View`, `Submission packet: N objects`, `Group k`…). Сказать: «одна разметка, два инструмента: в RenderDoc структура кадра, в Tracy время на видеокарте».
4. Открыть графики **Render: submitted** и **Render: texture switches**.
5. В редакторе снять галочку **Frustum culling**: на таймлайне появится сообщение `Frustum culling OFF`, график submitted прыгнет до 10 000, зоны `Submission packet` на CPU и GPU станут длиннее. Вернуть галочку.
6. То же с **Sort submission**: график texture switches вырастет с ~16 до тысяч.

Tracy и RenderDoc одновременно не запускать: каждый добавляет нагрузку, и цифры исказятся.

## 6. Если спросят

- **Что такое debug group?** Именованная скобка вокруг команд видеокарте (`glPushDebugGroup` / `glPopDebugGroup` из расширения KHR_debug). Видеокарта её не исполняет, её читают инструменты: RenderDoc, Nsight, драйверная отладка.
- **Сколько это стоит?** Порядка 20 пар вызовов за кадр (по группе на сочетание ресурсов). Имена групп собираются, только если приёмник разметки есть (`debugMarkersActive`); иначе вызовы пустые.
- **Почему разметка через адаптер?** Граница слоя: рендер и редактор не знают про OpenGL. Адаптер сам выбирает KHR_debug, EXT_debug_marker или ничего.
- **Почему на Mac нет GPU-зон Tracy?** Tracy сам отключает их для OpenGL на Apple: метки времени GL там ненадёжны. Группы на Mac всё равно ставятся через EXT_debug_marker и видны в Xcode.
- **Чем GPU-зона отличается от CPU-зоны?** CPU-зона меряет, сколько процессор отправлял команды. GPU-зона ставит метки времени в поток команд и меряет, сколько видеокарта их исполняла.

## 7. Если что-то не так

- **Оверлея RenderDoc нет / снимок не делается.** Запускать через Launch Application, а не Inject. Выключить оверлеи Steam, Discord, NVIDIA. Нажимать F12, когда фокус в окне движка.
- **В Event Browser нет папок Scene View / Submission packet.** В логе движка `debug markers: none`. Под RenderDoc KHR_debug есть всегда; значит, запущен не тот exe (старая сборка) или Debug-сборка без последних изменений.
- **Движок не видит сцену.** Неверная Working Directory: укажите папку, где лежит `assets`.
- **Нет строки OpenGL в Tracy.** Сборка не Release (Tracy активен только в Release/RelWithDebInfo), версия Tracy не 0.14.1, или профилировщик подключён к другому процессу.
- **Замеры для таблицы.** Снимать отдельно, без Tracy и RenderDoc: `tools/defense/04 - ЗАМЕРЫ`. Разметка добавила немного вызовов, поэтому при желании перезапустить замеры, чтобы цифры в отчёте соответствовали коду.

Проверено на Mac: сборка, 12 тестов, запуск редактора (разметка через EXT_debug_marker), Windows-ветка с GPU-зонами Tracy проверена компиляцией. На Windows-ноутбуке перед защитой обязательно пройти разделы 2–5 один раз.
