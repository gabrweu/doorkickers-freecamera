cmake_minimum_required(VERSION 3.20)
project(dk2_freecam VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# static CRT so the plugin doesn't depend on a VC++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Door Kickers 2 is 64-bit; configure with an x64 toolchain")
endif()

# A dk2ml (Door Kickers 2 Native Mod Loader) plugin. The loader's public headers are in dk2ml/ (see dk2ml/README.md).
add_library(dk2_freecam SHARED
    src/Freecam.cpp
    src/camera/Camera.cpp
    src/camera/Shadows.cpp
    src/game/Game.cpp
    src/settings/Settings.cpp
    src/ui/MenuButton.cpp
    src/ui/Options.cpp
    src/ui/Ui.cpp)
target_include_directories(dk2_freecam PRIVATE dk2ml src)
target_compile_definitions(dk2_freecam PRIVATE UNICODE _UNICODE PLUGIN_VERSION="${PROJECT_VERSION}") # the manifest
target_link_libraries(dk2_freecam PRIVATE user32)
