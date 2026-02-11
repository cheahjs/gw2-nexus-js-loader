cmake_minimum_required(VERSION 3.20)
project(gw2-nexus-js-loader VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Download CEF binary distribution
include(cmake/DownloadCEF.cmake)
include(cmake/CEFHelpers.cmake)

# ============================================================================
# Build CEF wrapper library from source
# ============================================================================
file(GLOB_RECURSE CEF_WRAPPER_SOURCES
    "${CEF_WRAPPER_DIR}/*.cc"
    "${CEF_WRAPPER_DIR}/*.h"
)

add_library(libcef_dll_wrapper STATIC ${CEF_WRAPPER_SOURCES})

target_include_directories(libcef_dll_wrapper PUBLIC
    "${CEF_ROOT}"
)

target_compile_definitions(libcef_dll_wrapper PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    WRAPPING_CEF_SHARED
    _WIN32
    _WINDOWS
    UNICODE
    _UNICODE
    __STDC_CONSTANT_MACROS
    __STDC_FORMAT_MACROS
    _HAS_EXCEPTIONS=0
)

target_compile_options(libcef_dll_wrapper PRIVATE
    /W0    # Suppress warnings in third-party code
    /EHsc
    /MT    # Static CRT for Release
)

# ============================================================================
# Shared sources (used by both plugin and subprocess)
# ============================================================================
set(SHARED_SOURCES
    src/shared/ipc_messages.h
    src/shared/version.h
)

# ============================================================================
# Main plugin DLL (browser process)
# ============================================================================
set(IMGUI_SOURCES
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
)

set(PLUGIN_SOURCES
    src/plugin/main.cpp
    src/plugin/globals.h
    src/plugin/globals.cpp
    src/plugin/cef_manager.h
    src/plugin/cef_manager.cpp
    src/plugin/browser_app.h
    src/plugin/browser_app.cpp
    src/plugin/browser_client.h
    src/plugin/browser_client.cpp
    src/plugin/osr_render_handler.h
    src/plugin/osr_render_handler.cpp
    src/plugin/d3d11_texture.h
    src/plugin/d3d11_texture.cpp
    src/plugin/input_handler.h
    src/plugin/input_handler.cpp
    src/plugin/overlay.h
    src/plugin/overlay.cpp
    src/plugin/ipc_handler.h
    src/plugin/ipc_handler.cpp
    src/plugin/web_app_manager.h
    src/plugin/web_app_manager.cpp
)

add_library(nexus_js_loader SHARED ${PLUGIN_SOURCES} ${IMGUI_SOURCES} ${SHARED_SOURCES})

target_include_directories(nexus_js_loader PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/third_party/nexus-api"
    "${CMAKE_SOURCE_DIR}/third_party/imgui"
    ${CEF_INCLUDE_DIR}
)

target_compile_definitions(nexus_js_loader PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32
    _WINDOWS
    UNICODE
    _UNICODE
)

target_compile_options(nexus_js_loader PRIVATE
    /W3
    /EHsc
    /MT
)

target_link_libraries(nexus_js_loader PRIVATE
    libcef_dll_wrapper
    "${CEF_LIB_DIR}/libcef.lib"
    d3d11.lib
    dxgi.lib
)

# Copy CEF runtime files next to the DLL
copy_cef_runtime_files(nexus_js_loader)

# Copy subprocess exe next to DLL after it's built
add_custom_command(TARGET nexus_js_loader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:nexus_js_subprocess>"
        "$<TARGET_FILE_DIR:nexus_js_loader>"
    COMMENT "Copying subprocess exe..."
)

# ============================================================================
# CEF subprocess executable (render process)
# ============================================================================
set(SUBPROCESS_SOURCES
    src/subprocess/main.cpp
    src/subprocess/renderer_app.h
    src/subprocess/renderer_app.cpp
    src/subprocess/js_bindings.h
    src/subprocess/js_bindings.cpp
    src/subprocess/ipc_client.h
    src/subprocess/ipc_client.cpp
)

add_executable(nexus_js_subprocess WIN32 ${SUBPROCESS_SOURCES} ${SHARED_SOURCES})

target_include_directories(nexus_js_subprocess PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    ${CEF_INCLUDE_DIR}
)

target_compile_definitions(nexus_js_subprocess PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32
    _WINDOWS
    UNICODE
    _UNICODE
)

target_compile_options(nexus_js_subprocess PRIVATE
    /W3
    /EHsc
    /MT
)

target_link_libraries(nexus_js_subprocess PRIVATE
    libcef_dll_wrapper
    "${CEF_LIB_DIR}/libcef.lib"
)

# Ensure subprocess is built before the main DLL
add_dependencies(nexus_js_loader nexus_js_subprocess)
