cmake_minimum_required(VERSION 3.16)
project(GameEngine VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- GLAD ---
add_library(glad STATIC external/glad/src/glad.c)
set_target_properties(glad PROPERTIES
    LINKER_LANGUAGE C
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib/Release"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/lib/Debug"
)
target_include_directories(glad PUBLIC external/glad/include)

# --- GLFW ---
add_library(glfw STATIC IMPORTED)
set_target_properties(glfw PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/external/glfw/lib-vc2022/glfw3.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/external/glfw/include"
)

# --- Assimp ---
# Попытка найти Assimp через find_package (если установлен через vcpkg)
find_package(assimp CONFIG)

if(NOT assimp_FOUND)
    # Если не найден через vcpkg, ищем вручную
    message(STATUS "Assimp not found via find_package, searching manually...")
    
    # Текущая структура: external/assimp/assimp/ содержит заголовки
    set(ASSIMP_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/assimp" CACHE PATH "Assimp include directory")
    set(ASSIMP_LIBRARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/assimp/lib" CACHE PATH "Assimp library directory")
    
    if(EXISTS "${ASSIMP_INCLUDE_DIR}/assimp/Importer.hpp")
        message(STATUS "Found Assimp headers at: ${ASSIMP_INCLUDE_DIR}")
        
        # Ищем .lib файл
        if(EXISTS "${ASSIMP_LIBRARY_DIR}/assimp-vc143-mt.lib")
            message(STATUS "Found Assimp library at: ${ASSIMP_LIBRARY_DIR}/assimp-vc143-mt.lib")
            add_library(assimp STATIC IMPORTED)
            set_target_properties(assimp PROPERTIES
                IMPORTED_LOCATION "${ASSIMP_LIBRARY_DIR}/assimp-vc143-mt.lib"
                INTERFACE_INCLUDE_DIRECTORIES "${ASSIMP_INCLUDE_DIR}"
            )
        else()
            message(WARNING "Assimp .lib file not found! Looking for DLL to use as import library...")
            # Если .lib нет, пробуем использовать DLL напрямую (не рекомендуется)
            if(EXISTS "${ASSIMP_LIBRARY_DIR}/assimp-vc143-mt.dll")
                message(STATUS "Found Assimp DLL, but .lib file is missing. Please add assimp-vc143-mt.lib to lib/ folder")
                add_library(assimp INTERFACE)
                target_include_directories(assimp INTERFACE "${ASSIMP_INCLUDE_DIR}")
            else()
                message(FATAL_ERROR "Assimp library files not found! Please add assimp-vc143-mt.lib to external/assimp/lib/")
            endif()
        endif()
    else()
        message(FATAL_ERROR "Assimp headers not found! Please install via vcpkg or manually. See ASSIMP_SETUP.md")
    endif()
endif()

# --- Game target ---
add_executable(GameEngine
    src/main.cpp
    src/core/Application.cpp
    src/ecs/RenderSystem.cpp
    src/ecs/SpinSystem.cpp
    src/ecs/World.cpp
    src/render/OpenGLRenderAdapter.cpp
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
    external/glfw/include
    external/stb_image
    src
)

target_link_libraries(GameEngine PRIVATE
    glad
    glfw
    opengl32
)

# Линкуем Assimp
if(assimp_FOUND)
    target_link_libraries(GameEngine PRIVATE assimp::assimp)
else()
    target_link_libraries(GameEngine PRIVATE assimp)
endif()

add_custom_target(CopyAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:GameEngine>/assets"
)
add_dependencies(CopyAssets GameEngine)

# Копируем DLL Assimp если нужно
if(EXISTS "${ASSIMP_LIBRARY_DIR}/assimp-vc143-mt.dll")
    add_custom_command(TARGET GameEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ASSIMP_LIBRARY_DIR}/assimp-vc143-mt.dll"
        "$<TARGET_FILE_DIR:GameEngine>"
    )
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/assimp/assimp-vc143-mt.dll")
    add_custom_command(TARGET GameEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/external/assimp/assimp-vc143-mt.dll"
        "$<TARGET_FILE_DIR:GameEngine>"
    )
endif()
