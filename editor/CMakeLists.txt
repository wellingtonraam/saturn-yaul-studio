cmake_minimum_required(VERSION 3.5...3.30)
project(SaturnYaulStudio VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(POLICY CMP0048)
  cmake_policy(SET CMP0048 NEW)
endif()
if(POLICY CMP0077)
  cmake_policy(SET CMP0077 NEW)
endif()

if(MSVC)
    add_compile_options(/W0)
else()
    add_compile_options(-w)
endif()

include(FetchContent)

# 1. Dear ImGui (docking)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

# 2. Raylib 5.5
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

# 3. rlImGui (main)
FetchContent_Declare(
    rlImGui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(rlImGui)

add_executable(SaturnStudio 
    src/main.cpp
    src/platform_dialogs.cpp
    src/app_icon.cpp
    src/scene.cpp
    src/scene_serializer.cpp
    src/project.cpp
    src/project.h
    src/gizmo.cpp
    src/yaul_exporter.cpp
    resources.rc
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)

target_include_directories(SaturnStudio PRIVATE 
    src
    ${imgui_SOURCE_DIR}
    ${rlimgui_SOURCE_DIR}
)

# Adicionada shell32 para suporte a diálogo de seleção de pastas
target_link_libraries(SaturnStudio PRIVATE raylib comdlg32 shell32)