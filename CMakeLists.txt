cmake_minimum_required(VERSION 3.16)
project(GameEngine VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Tracy (vendored client, no network or vcpkg required) ---
option(ENGINE_ENABLE_TRACY "Enable Tracy in Release and RelWithDebInfo" ON)
set(ENGINE_TRACY_SAMPLING_HZ "4000" CACHE STRING "Tracy sampling frequency in Hz")
option(ENGINE_TRACY_MANUAL_SAMPLING "Start Tracy sampling explicitly from demo controls" OFF)
option(ENGINE_TRACY_NO_SAMPLING "Disable Tracy stack sampling" OFF)
if(NOT ENGINE_TRACY_SAMPLING_HZ MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "ENGINE_TRACY_SAMPLING_HZ must be a positive integer")
endif()
add_library(TracyHeaders INTERFACE)
target_include_directories(TracyHeaders SYSTEM INTERFACE external/tracy/public)
add_library(TracyClient STATIC external/tracy/public/TracyClient.cpp)
add_library(Tracy::TracyClient ALIAS TracyClient)
target_link_libraries(TracyClient PUBLIC TracyHeaders)
find_package(Threads REQUIRED)
target_link_libraries(TracyClient PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(TracyClient PRIVATE ws2_32 dbghelp secur32)
endif()
# PUBLIC keeps the client and all instrumented translation units consistent.
set(ENGINE_TRACY_ACTIVE "$<AND:$<BOOL:${ENGINE_ENABLE_TRACY}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>")
target_compile_definitions(TracyClient PUBLIC
    "$<${ENGINE_TRACY_ACTIVE}:TRACY_ENABLE>"
    "$<${ENGINE_TRACY_ACTIVE}:TRACY_ON_DEMAND>"
    "$<${ENGINE_TRACY_ACTIVE}:TRACY_SAMPLING_HZ=${ENGINE_TRACY_SAMPLING_HZ}>"
    "$<$<AND:${ENGINE_TRACY_ACTIVE},$<BOOL:${ENGINE_TRACY_MANUAL_SAMPLING}>>:TRACY_SAMPLING_PROFILER_MANUAL_START>"
    "$<$<AND:${ENGINE_TRACY_ACTIVE},$<BOOL:${ENGINE_TRACY_NO_SAMPLING}>>:TRACY_NO_SAMPLING>"
)

# --- GLAD ---
add_library(glad STATIC external/glad/src/glad.c)
set_target_properties(glad PROPERTIES
    LINKER_LANGUAGE C
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib/Release"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/lib/Debug"
)
target_include_directories(glad PUBLIC external/glad/include)

# --- GLFW ---
if(WIN32)
    add_library(glfw STATIC IMPORTED)
    set_target_properties(glfw PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/external/glfw/lib-vc2022/glfw3.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/external/glfw/include"
    )
else()
    # macOS / Linux: системный GLFW (brew install glfw), экспортирует target `glfw`
    find_package(glfw3 3.3 REQUIRED)
endif()

# --- OpenGL ---
if(WIN32)
    set(ENGINE_OPENGL_LIB opengl32)
else()
    find_package(OpenGL REQUIRED)
    set(ENGINE_OPENGL_LIB OpenGL::GL)
endif()

# --- Dear ImGui ---
add_library(imgui STATIC
    external/imgui/imgui.cpp
    external/imgui/imgui_demo.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_glfw.cpp
    external/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    external/imgui
    external/imgui/backends
    external/glad/include
)
if(WIN32)
    target_include_directories(imgui PUBLIC external/glfw/include)
else()
    target_link_libraries(imgui PUBLIC glfw)
endif()
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD)

# --- ImGuizmo ---
add_library(ImGuizmo STATIC
    external/ImGuizmo/ImGuizmo.cpp
)
target_include_directories(ImGuizmo PUBLIC
    external/ImGuizmo
    external/imgui
)
target_link_libraries(ImGuizmo PUBLIC imgui)

option(ENGINE_BUILD_TRACY_DEMOS "Build the five ImGui + D3D12 Tracy demos" ON)
if(ENGINE_BUILD_TRACY_DEMOS AND WIN32)
    add_subdirectory(demos/tracy)
endif()

# --- Assimp ---
# Попытка найти Assimp через find_package (если установлен через vcpkg)
find_package(assimp CONFIG)

if(NOT assimp_FOUND)
    # Если не найден через vcpkg, ищем вручную
    message(STATUS "Assimp not found via find_package, searching manually...")
    
    # Текущая структура: external/assimp/assimp/ содержит заголовки
    set(ASSIMP_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/assimp" CACHE PATH "Assimp include directory")
    set(ASSIMP_LIBRARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/assimp/lib" CACHE PATH "Assimp library directory")
    
    if(NOT EXISTS "${ASSIMP_INCLUDE_DIR}/assimp/Importer.hpp")
        message(FATAL_ERROR "Assimp headers not found. See ASSIMP_SETUP.md")
    endif()
    if(NOT EXISTS "${ASSIMP_LIBRARY_DIR}/assimp-vc143-mt.lib")
        message(FATAL_ERROR "Assimp import library missing: ${ASSIMP_LIBRARY_DIR}/assimp-vc143-mt.lib")
    endif()
    set(ASSIMP_RUNTIME_DLL "${ASSIMP_LIBRARY_DIR}/assimp-vc143-mt.dll")
    if(NOT EXISTS "${ASSIMP_RUNTIME_DLL}")
        set(ASSIMP_RUNTIME_DLL "${CMAKE_CURRENT_SOURCE_DIR}/external/assimp/assimp-vc143-mt.dll")
    endif()
    if(NOT EXISTS "${ASSIMP_RUNTIME_DLL}")
        message(FATAL_ERROR "Assimp runtime DLL missing. Add assimp-vc143-mt.dll next to the import library.")
    endif()
    # The .lib is an import library; Assimp itself is a DLL.
    add_library(assimp SHARED IMPORTED)
    set_target_properties(assimp PROPERTIES
        IMPORTED_IMPLIB "${ASSIMP_LIBRARY_DIR}/assimp-vc143-mt.lib"
        IMPORTED_LOCATION "${ASSIMP_RUNTIME_DLL}"
        INTERFACE_INCLUDE_DIRECTORIES "${ASSIMP_INCLUDE_DIR}"
    )
endif()

if(assimp_FOUND)
    set(ENGINE_ASSIMP_TARGET assimp::assimp)
else()
    set(ENGINE_ASSIMP_TARGET assimp)
endif()

function(deploy_assimp_runtime target)
    get_target_property(assimp_type ${ENGINE_ASSIMP_TARGET} TYPE)
    if(WIN32 AND assimp_type STREQUAL "SHARED_LIBRARY")
        # Generate the paths separately so they do not create a dependency cycle
        # between the executable and its always-run runtime preparation target.
        set(copy_script "${CMAKE_CURRENT_BINARY_DIR}/${target}-assimp-$<CONFIG>.cmake")
        file(GENERATE OUTPUT "${copy_script}" CONTENT
"file(MAKE_DIRECTORY [==[$<TARGET_FILE_DIR:${target}>]==])
execute_process(COMMAND [==[${CMAKE_COMMAND}]==] -E copy_if_different
    [==[$<TARGET_FILE:${ENGINE_ASSIMP_TARGET}>]==] [==[$<TARGET_FILE_DIR:${target}>]==]
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR \"Failed to deploy Assimp runtime for ${target}\")
endif()
")
        add_custom_target(${target}Runtime
            COMMAND "${CMAKE_COMMAND}" -P "${copy_script}"
            COMMENT "Ensuring Assimp runtime is present for ${target}"
            VERBATIM
        )
        add_dependencies(${target} ${target}Runtime)
        # MSBuild can remove stale outputs while relinking. Restore the runtime
        # after linking as well as on builds where the executable is up to date.
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -P "${copy_script}"
            VERBATIM
        )
    endif()
endfunction()

include(CTest)
if(BUILD_TESTING)
    add_executable(PhysicsSystemTests
        tests/PhysicsSystemTests.cpp
        src/ecs/PhysicsSystem.cpp
        src/ecs/World.cpp
    )
    target_include_directories(PhysicsSystemTests PRIVATE src)
    target_link_libraries(PhysicsSystemTests PRIVATE TracyHeaders)
    add_test(NAME PhysicsSystemTests COMMAND PhysicsSystemTests)

    add_executable(CoordinateSystemTests
        tests/CoordinateSystemTests.cpp
        src/editor/EditorCamera.cpp
        src/input/InputManager.cpp
        src/ecs/PhysicsSystem.cpp
        src/ecs/SpinSystem.cpp
        src/ecs/World.cpp
        src/resources/SceneManifest.cpp
    )
    target_include_directories(CoordinateSystemTests PRIVATE src external)
    target_link_libraries(CoordinateSystemTests PRIVATE TracyHeaders)
    add_test(NAME CoordinateSystemTests COMMAND CoordinateSystemTests)
    set_tests_properties(CoordinateSystemTests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# --- Game target ---
add_executable(GameEngine
    src/main.cpp
    src/core/Application.cpp
    src/ecs/CameraSystem.cpp
    src/ecs/DebugRenderSystem.cpp
    src/ecs/PhysicsSystem.cpp
    src/ecs/RenderSystem.cpp
    src/ecs/SpinSystem.cpp
    src/ecs/World.cpp
    src/input/GlfwInputHandler.cpp
    src/input/InputManager.cpp
    src/render/OpenGLRenderAdapter.cpp
    src/editor/EditorCamera.cpp
    src/editor/TexturePicker.cpp
    src/states/EditorState.cpp
    src/states/StateManager.cpp
    src/states/LoadingState.cpp
    src/states/MenuState.cpp
    src/states/GameplayState.cpp
    src/render/ShaderProgram.cpp
    src/resources/ResourceManager.cpp
    src/resources/MeshLoader.cpp
    src/resources/TextureLoader.cpp
    src/resources/ShaderLoader.cpp
    src/resources/SceneManifest.cpp
    src/resources/HotReload.cpp
    src/resources/stb_image_impl.cpp
)

target_include_directories(GameEngine PRIVATE
    external
    external/glad/include
    external/imgui
    external/imgui/backends
    external/ImGuizmo
    external/stb_image
    src
)

if(WIN32)
    target_include_directories(GameEngine PRIVATE external/glfw/include)
endif()

target_link_libraries(GameEngine PRIVATE
    glad
    glfw
    imgui
    ImGuizmo
    ${ENGINE_OPENGL_LIB}
    Tracy::TracyClient
)

target_link_libraries(GameEngine PRIVATE ${ENGINE_ASSIMP_TARGET})
deploy_assimp_runtime(GameEngine)

add_custom_target(CopyAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:GameEngine>/assets"
)
add_dependencies(CopyAssets GameEngine)

if(BUILD_TESTING)
    add_executable(TextureResourceTests
        tests/TextureResourceTests.cpp
        src/ecs/World.cpp
        src/ecs/RenderSystem.cpp
        src/render/OpenGLRenderAdapter.cpp
        src/render/ShaderProgram.cpp
        src/resources/ResourceManager.cpp
        src/resources/MeshLoader.cpp
        src/resources/TextureLoader.cpp
        src/resources/ShaderLoader.cpp
        src/resources/stb_image_impl.cpp
    )
    target_include_directories(TextureResourceTests PRIVATE src external/stb_image)
    target_link_libraries(TextureResourceTests PRIVATE glad glfw ${ENGINE_OPENGL_LIB})
    target_link_libraries(TextureResourceTests PRIVATE TracyHeaders)
    target_link_libraries(TextureResourceTests PRIVATE ${ENGINE_ASSIMP_TARGET})
    deploy_assimp_runtime(TextureResourceTests)
    add_test(NAME TextureResourceTests COMMAND TextureResourceTests "${CMAKE_CURRENT_BINARY_DIR}/texture-test-fixtures")
    set_tests_properties(TextureResourceTests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        SKIP_RETURN_CODE 77
    )
endif()
