# Lua is compiled into the executable; no system interpreter or runtime DLL.
# Подключает Lua и sol2 как исходники из external/ и собирает статическую библиотеку EngineLua.
# Её линкуют движок, редактор и тесты скриптов (ScriptSystemTests, ScriptRuntimeTests).
# Сами исходники скачивает tools/setup_lua.sh (или setup_lua.ps1), в git их нет: external/lua и external/sol2 в .gitignore.
# Пути можно переопределить при конфигурации: -DENGINE_LUA_ROOT=... и -DENGINE_SOL_ROOT=...
set(ENGINE_LUA_ROOT "${CMAKE_SOURCE_DIR}/external/lua/lua-5.4.8" CACHE PATH "Lua source directory")
set(ENGINE_SOL_ROOT "${CMAKE_SOURCE_DIR}/external/sol2/sol2-3.3.1" CACHE PATH "sol2 source directory")
# Без исходников собирать нечего: останавливаемся сразу и говорим, какой скрипт запустить.
if(NOT EXISTS "${ENGINE_LUA_ROOT}/src/lua.h" OR NOT EXISTS "${ENGINE_SOL_ROOT}/include/sol/sol.hpp")
    message(FATAL_ERROR "Lua/sol2 sources are missing. Run tools/setup_lua.sh (macOS/Linux) "
        "or powershell -ExecutionPolicy Bypass -File tools/setup_lua.ps1 (Windows) first")
endif()
# sol2 3.3.1: optional<T&>::emplace calls a member that does not exist (fixed upstream
# after the release). MSVC and GCC never look at the uninstantiated template, clang 19+ does.
# Patch a copy in the build tree; the downloaded sources stay as the setup script left them.
# Как устроен патч: читаем optional_implementation.hpp и режем текст на две части по месту «class optional<T&>»
# (до него обычный optional, после него версия для ссылок). Замену делаем только во второй части, чтобы не задеть
# обычный optional. Результат пишем во временный файл и копируем на место с ONLY_IF_DIFFERENT: если содержимое
# не изменилось, время файла остаётся прежним, и всё, что включает sol2, не пересобирается при каждой конфигурации.
set(ENGINE_SOL_PATCH_DIR "${CMAKE_BINARY_DIR}/sol2-patched")
file(READ "${ENGINE_SOL_ROOT}/include/sol/optional_implementation.hpp" ENGINE_SOL_OPTIONAL)
string(FIND "${ENGINE_SOL_OPTIONAL}" "class optional<T&>" ENGINE_SOL_REFERENCE_AT)
string(SUBSTRING "${ENGINE_SOL_OPTIONAL}" 0 ${ENGINE_SOL_REFERENCE_AT} ENGINE_SOL_HEAD)
string(SUBSTRING "${ENGINE_SOL_OPTIONAL}" ${ENGINE_SOL_REFERENCE_AT} -1 ENGINE_SOL_TAIL)
string(REPLACE "this->construct(std::forward<Args>(args)...);"
    "*this = optional(std::forward<Args>(args)...);\n\t\t\treturn **this;" ENGINE_SOL_TAIL "${ENGINE_SOL_TAIL}")
file(WRITE "${ENGINE_SOL_PATCH_DIR}/sol/optional_implementation.hpp.tmp" "${ENGINE_SOL_HEAD}${ENGINE_SOL_TAIL}")
file(COPY_FILE "${ENGINE_SOL_PATCH_DIR}/sol/optional_implementation.hpp.tmp"
    "${ENGINE_SOL_PATCH_DIR}/sol/optional_implementation.hpp" ONLY_IF_DIFFERENT)

# Берём все .c файлы ядра Lua, кроме lua.c (интерпретатор) и luac.c (компилятор): у них свой main().
# CONFIGURE_DEPENDS: CMake сам заметит добавленные или удалённые файлы.
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "${ENGINE_LUA_ROOT}/src/*.c")
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|luac)\\.c$")
# Lua собирается как статическая библиотека и вшивается в исполняемый файл.
add_library(EngineLua STATIC ${LUA_SOURCES})
# PUBLIC: заголовки нужны и тем, кто линкует EngineLua. SYSTEM: предупреждения из чужих заголовков не показываются.
# Папка с патчем стоит раньше include самого sol2, поэтому подхватывается исправленный optional_implementation.hpp.
target_include_directories(EngineLua SYSTEM PUBLIC "${ENGINE_LUA_ROOT}/src" "${ENGINE_SOL_PATCH_DIR}" "${ENGINE_SOL_ROOT}/include")
# sol2: включить все проверки безопасности (типы и число аргументов, nil), чтобы ошибка скрипта давала сообщение,
# а не неопределённое поведение. PUBLIC, чтобы все, кто подключает sol2, собирались с одинаковыми настройками.
target_compile_definitions(EngineLua PUBLIC SOL_ALL_SAFETIES_ON=1)
# LUA_USE_POSIX включает в Lua POSIX-функции (popen, mkstemp, gmtime_r) и нужен только при сборке самой Lua (PRIVATE).
# m это libm для математики, CMAKE_DL_LIBS это libdl на Linux.
if(UNIX)
    target_compile_definitions(EngineLua PRIVATE LUA_USE_POSIX)
    target_link_libraries(EngineLua PUBLIC m ${CMAKE_DL_LIBS})
endif()
