cmake_minimum_required(VERSION 3.25)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(FrameSnap LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(FrameSnap WIN32
    resources/app.rc
    src/app.cpp
    src/app.h
    src/capture_engine.cpp
    src/capture_engine.h
    src/clipboard_publisher.cpp
    src/clipboard_publisher.h
    src/color_picker.cpp
    src/color_picker.h
    src/common.h
    src/editor_window.cpp
    src/editor_window.h
    src/image_io.cpp
    src/image_io.h
    src/main.cpp
    src/overlay_window.cpp
    src/overlay_window.h
    src/preview_window.cpp
    src/preview_window.h
    src/save_queue.cpp
    src/save_queue.h
    src/settings_store.cpp
    src/settings_store.h
    src/settings_window.cpp
    src/settings_window.h
    src/types.h
    src/util.cpp
    src/util.h
)

target_compile_definitions(FrameSnap PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_compile_options(FrameSnap PRIVATE
    /W4
    /permissive-
    /EHsc
    /utf-8
    /Zc:__cplusplus
)

target_link_libraries(FrameSnap PRIVATE
    advapi32
    comctl32
    d2d1
    d3d11
    dwmapi
    dxgi
    gdiplus
    ole32
    shcore
    shell32
    shlwapi
    windowscodecs
    winmm
)

set_target_properties(FrameSnap PROPERTIES
    WIN32_EXECUTABLE YES
    VS_GLOBAL_DpiAwareness "PerMonitorV2"
)
