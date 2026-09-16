add_library(TracyDemoPlatform STATIC
    ../../external/imgui/backends/imgui_impl_win32.cpp
    ../../external/imgui/backends/imgui_impl_dx12.cpp
)
target_link_libraries(TracyDemoPlatform PUBLIC imgui d3d12 dxgi d3dcompiler dwmapi)
target_compile_definitions(TracyDemoPlatform PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

option(ENGINE_TRACY_DEMO_DETAIL "Enable extra zone call stacks in Tracy demos" OFF)
foreach(demo IN ITEMS zones memory locks messages gpu)
    add_executable(demo_${demo} demo_${demo}.cpp)
    target_link_libraries(demo_${demo} PRIVATE TracyDemoPlatform Tracy::TracyClient)
    target_compile_definitions(demo_${demo} PRIVATE $<$<BOOL:${ENGINE_TRACY_DEMO_DETAIL}>:TRACY_DEMO_DETAIL>)
    if(MSVC)
        target_compile_options(demo_${demo} PRIVATE /utf-8 /Zi)
        target_link_options(demo_${demo} PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    endif()
    set_target_properties(demo_${demo} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tracy-demos/$<CONFIG>"
    )
endforeach()
target_sources(demo_memory PRIVATE MemoryTracking.cpp)
add_custom_target(TracyDemos DEPENDS demo_zones demo_memory demo_locks demo_messages demo_gpu)
