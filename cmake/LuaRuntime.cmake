# Lua is compiled into the executable; no system interpreter or runtime DLL.
set(ENGINE_LUA_ROOT "${CMAKE_SOURCE_DIR}/external/lua/lua-5.4.8" CACHE PATH "Lua source directory")
set(ENGINE_SOL_ROOT "${CMAKE_SOURCE_DIR}/external/sol2/sol2-3.3.1" CACHE PATH "sol2 source directory")
if(NOT EXISTS "${ENGINE_LUA_ROOT}/src/lua.h" OR NOT EXISTS "${ENGINE_SOL_ROOT}/include/sol/sol.hpp")
    message(FATAL_ERROR "Run powershell -ExecutionPolicy Bypass -File tools/setup_lua.ps1 first")
endif()
# sol2 3.3.1: optional<T&>::emplace calls a member that does not exist (fixed upstream
# after the release). MSVC and GCC never look at the uninstantiated template, clang 19+ does.
# Patch a copy in the build tree; the downloaded sources stay as setup_lua.ps1 left them.
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

file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "${ENGINE_LUA_ROOT}/src/*.c")
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|luac)\\.c$")
add_library(EngineLua STATIC ${LUA_SOURCES})
target_include_directories(EngineLua SYSTEM PUBLIC "${ENGINE_LUA_ROOT}/src" "${ENGINE_SOL_PATCH_DIR}" "${ENGINE_SOL_ROOT}/include")
target_compile_definitions(EngineLua PUBLIC SOL_ALL_SAFETIES_ON=1)
if(UNIX)
    target_compile_definitions(EngineLua PRIVATE LUA_USE_POSIX)
    target_link_libraries(EngineLua PUBLIC m ${CMAKE_DL_LIBS})
endif()
