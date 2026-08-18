cmake_minimum_required(VERSION 3.20)
project(UwUClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── UwUClient.exe ────────────────────────────────────────────────────────────
add_executable(UwUClient WIN32
    src/main.cpp
    src/overlay.cpp
    src/esp.cpp
    src/aimbot.cpp
    src/DebugTab.cpp
    src/executor.cpp
    src/bgimage.cpp
    src/dumper.cpp

    # Void-theme ImGui menu (single file)
    src/menu.cpp

    resources.rc

    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/backends/imgui_impl_dx11.cpp
    vendor/imgui/backends/imgui_impl_win32.cpp
)

target_include_directories(UwUClient PRIVATE
    include/
    vendor/imgui/
    vendor/imgui/backends/
)

target_link_libraries(UwUClient PRIVATE
    d3d11
    dxgi
    dwmapi
    winhttp
    windowscodecs
    winmm
)

target_compile_definitions(UwUClient PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

set_target_properties(UwUClient PROPERTIES OUTPUT_NAME "UwUClient")

if(MSVC)
    set_property(TARGET UwUClient APPEND_STRING PROPERTY LINK_FLAGS " /MANIFEST:NO")
endif()

# Back to static CRT — ship one exe, nothing else required.
set_property(TARGET UwUClient PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Ship bg.jpg next to the exe so bgimage::load finds it at runtime.
if(EXISTS "${CMAKE_SOURCE_DIR}/bg.jpg")
    add_custom_command(TARGET UwUClient POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/bg.jpg"
                "$<TARGET_FILE_DIR:UwUClient>/bg.jpg")
endif()
