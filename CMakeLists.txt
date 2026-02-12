cmake_minimum_required(VERSION 3.20)
project(gw2-nexus-js-loader VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Download CEF binary distribution (still needed for headers + wrapper source)
include(cmake/DownloadCEF.cmake)

# ============================================================================
# Build CEF wrapper library from source (static library)
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
# Shared sources (used by plugin)
# ============================================================================
set(SHARED_SOURCES
    src/shared/version.h
)

# ============================================================================
# Main plugin DLL (in-process CEF via delay-load)
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
    src/plugin/cef_loader.h
    src/plugin/cef_loader.cpp
    src/plugin/in_process_browser.h
    src/plugin/in_process_browser.cpp
    src/plugin/nexus_bridge.h
    src/plugin/nexus_bridge.cpp
    src/plugin/d3d11_texture.h
    src/plugin/d3d11_texture.cpp
    src/plugin/input_handler.h
    src/plugin/input_handler.cpp
    src/plugin/overlay.h
    src/plugin/overlay.cpp
    src/plugin/ipc_handler.h
    src/plugin/ipc_handler.cpp
    src/plugin/addon_manager.h
    src/plugin/addon_manager.cpp
    src/plugin/addon_instance.h
    src/plugin/addon_instance.cpp
    src/plugin/addon_scheme_handler.h
    src/plugin/addon_scheme_handler.cpp
)

add_library(nexus_js_loader SHARED ${PLUGIN_SOURCES} ${IMGUI_SOURCES} ${SHARED_SOURCES})

target_include_directories(nexus_js_loader PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/third_party/nexus-api"
    "${CMAKE_SOURCE_DIR}/third_party/imgui"
    "${CMAKE_SOURCE_DIR}/third_party"
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

# Delay-load libcef.dll — resolved at runtime to GW2's already-loaded copy
target_link_options(nexus_js_loader PRIVATE
    /DELAYLOAD:libcef.dll
)

target_link_libraries(nexus_js_loader PRIVATE
    libcef_dll_wrapper
    "${CEF_LIB_DIR}/libcef.lib"
    delayimp.lib
    d3d11.lib
    dxgi.lib
)
