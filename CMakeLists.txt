cmake_minimum_required(VERSION 3.20)
project(dk2_freecam VERSION 4 LANGUAGES C CXX)

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
    src/camera/CameraInput.cpp
    src/camera/CameraLimits.cpp
    src/camera/CameraLockedView.cpp
    src/camera/CameraOrbit.cpp
    src/camera/Shadows.cpp
    src/camera/Icons.cpp
    src/game/Game.cpp
    src/settings/Settings.cpp
    src/ui/HudButton.cpp
    src/ui/MenuButton.cpp
    src/ui/Options.cpp
    src/ui/Ui.cpp)
target_include_directories(dk2_freecam PRIVATE dk2ml src ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(dk2_freecam PRIVATE UNICODE _UNICODE PLUGIN_VERSION="${PROJECT_VERSION}") # the manifest
target_link_libraries(dk2_freecam PRIVATE user32)

# BuildTime.h (PLUGIN_BUILD_TIME), rewritten every build: __TIME__ would keep the time of the file's last compile
set(BUILD_TIME_HEADER ${CMAKE_BINARY_DIR}/generated/BuildTime.h)
add_custom_target(build_time ALL
    COMMAND ${CMAKE_COMMAND} -DOUT=${BUILD_TIME_HEADER} -P ${CMAKE_SOURCE_DIR}/cmake/BuildTime.cmake
    BYPRODUCTS ${BUILD_TIME_HEADER})
add_dependencies(dk2_freecam build_time)
