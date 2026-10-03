cmake_minimum_required(VERSION 3.25)
project(FrameSnap VERSION 0.2.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)

if(BUILD_TESTING)
    add_executable(FrameSnapPolicyTests tests/policy_tests.cpp)
    target_include_directories(FrameSnapPolicyTests PRIVATE src)
    add_test(NAME policy COMMAND FrameSnapPolicyTests)
endif()

if(NOT WIN32)
    message(STATUS "FrameSnap requires Windows. Only portable policy tests are built on this platform.")
    return()
endif()

enable_language(RC)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(FRAMESNAP_LIBRARIES advapi32 comctl32 comdlg32 d2d1 d3d11 dwmapi dxgi gdiplus
    gdi32 ole32 shcore shell32 shlwapi user32 uxtheme windowscodecs winmm uuid)
function(framesnap_windows_target target)
    target_include_directories(${target} PRIVATE src)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
    target_link_libraries(${target} PRIVATE ${FRAMESNAP_LIBRARIES})
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus /guard:cf)
        target_link_options(${target} PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /CETCOMPAT)
    elseif(MINGW)
        target_compile_options(${target} PRIVATE -Wall -Wextra)
        target_link_options(${target} PRIVATE -static -Wl,--dynamicbase,--nxcompat,--high-entropy-va)
    endif()
endfunction()

add_executable(FrameSnap WIN32
    resources/app.rc src/app.cpp src/capture_engine.cpp src/clipboard_publisher.cpp
    src/color_picker.cpp src/editor_window.cpp src/image_io.cpp src/main.cpp
    src/overlay_window.cpp src/preview_window.cpp src/save_queue.cpp
    src/settings_store.cpp src/settings_window.cpp src/util.cpp)
framesnap_windows_target(FrameSnap)
target_include_directories(FrameSnap PRIVATE resources)
if(MSVC)
    # Let the linker merge our manifest with its generated manifest, then embed
    # it once. Compiling it into app.rc too creates duplicate resource ID 1.
    target_sources(FrameSnap PRIVATE resources/app.manifest)
    target_compile_definitions(FrameSnap PRIVATE FRAMESNAP_LINKER_MANIFEST)
elseif(MINGW)
    target_link_options(FrameSnap PRIVATE -municode)
endif()

if(BUILD_TESTING)
    if(MINGW)
        target_link_options(FrameSnapPolicyTests PRIVATE -static)
    endif()
    add_executable(FrameSnapWindowsTests tests/windows_tests.cpp src/clipboard_publisher.cpp
        src/image_io.cpp src/save_queue.cpp src/settings_store.cpp src/util.cpp)
    framesnap_windows_target(FrameSnapWindowsTests)
    add_test(NAME windows_components COMMAND FrameSnapWindowsTests)
    add_test(NAME windows_lifecycle COMMAND FrameSnapWindowsTests $<TARGET_FILE:FrameSnap>)
    set_tests_properties(windows_components windows_lifecycle PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
endif()

install(TARGETS FrameSnap RUNTIME DESTINATION .)
install(FILES README.md LICENSE DESTINATION .)
install(FILES docs/product-review.md docs/windows-security.md DESTINATION docs)
install(FILES scripts/install.ps1 DESTINATION . RENAME Install.ps1)
install(FILES scripts/uninstall.ps1 DESTINATION . RENAME Uninstall.ps1)
install(FILES scripts/Install.cmd scripts/Uninstall.cmd DESTINATION .)
set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "FrameSnap-${PROJECT_VERSION}-windows-x64")
include(CPack)
