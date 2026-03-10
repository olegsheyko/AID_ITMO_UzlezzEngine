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

# --- Game target ---
add_executable(GameEngine
    src/main.cpp
    src/core/Application.cpp
    src/render/OpenGLRenderAdapter.cpp
    src/states/StateManager.cpp
    src/states/LoadingState.cpp
    src/states/MenuState.cpp
    src/states/GameplayState.cpp
    src/render/ShaderProgram.cpp
)

target_include_directories(GameEngine PRIVATE
    external/glad/include
    external/glfw/include
    src
)

target_link_libraries(GameEngine PRIVATE
    glad
    glfw
    opengl32
)

add_custom_command(TARGET GameEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:GameEngine>/assets"
)
