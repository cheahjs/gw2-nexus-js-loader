# Download CEF binary distribution at configure time
# Uses the Spotify CDN for CEF builds

set(CEF_VERSION "145.0.22+g0fa8d1b+chromium-145.0.7632.45" CACHE STRING "CEF version to download")
set(CEF_PLATFORM "windows64" CACHE STRING "CEF platform")

# URL-encode the version string (+ → %2B)
string(REPLACE "+" "%2B" CEF_VERSION_ENCODED "${CEF_VERSION}")

set(CEF_DISTRIBUTION "cef_binary_${CEF_VERSION}_${CEF_PLATFORM}_minimal")
string(REPLACE "+" "%2B" CEF_DISTRIBUTION_ENCODED "${CEF_DISTRIBUTION}")

set(CEF_DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/third_party/cef")
# Use a simple directory name to avoid Ninja path-escaping issues with '+' chars
set(CEF_ROOT "${CEF_DOWNLOAD_DIR}/cef_dist")

if(NOT EXISTS "${CEF_ROOT}")
    set(CEF_URL "https://cef-builds.spotifycdn.com/${CEF_DISTRIBUTION_ENCODED}.tar.bz2")

    message(STATUS "Downloading CEF from ${CEF_URL}")
    message(STATUS "This may take a while...")

    file(DOWNLOAD
        "${CEF_URL}"
        "${CEF_DOWNLOAD_DIR}/cef.tar.bz2"
        SHOW_PROGRESS
        STATUS DOWNLOAD_STATUS
    )

    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    if(NOT STATUS_CODE EQUAL 0)
        list(GET DOWNLOAD_STATUS 1 ERROR_MESSAGE)
        message(FATAL_ERROR "Failed to download CEF: ${ERROR_MESSAGE}")
    endif()

    message(STATUS "Extracting CEF...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xjf "${CEF_DOWNLOAD_DIR}/cef.tar.bz2"
        WORKING_DIRECTORY "${CEF_DOWNLOAD_DIR}"
        RESULT_VARIABLE EXTRACT_RESULT
    )

    if(NOT EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to extract CEF")
    endif()

    file(REMOVE "${CEF_DOWNLOAD_DIR}/cef.tar.bz2")

    # Rename the extracted directory to a simple name (avoids '+' in paths breaking Ninja)
    file(RENAME "${CEF_DOWNLOAD_DIR}/${CEF_DISTRIBUTION}" "${CEF_ROOT}")
endif()

message(STATUS "CEF root: ${CEF_ROOT}")

# Set up CEF variables for use in the main CMakeLists
set(CEF_INCLUDE_DIR "${CEF_ROOT}" "${CEF_ROOT}/include")
set(CEF_LIB_DIR "${CEF_ROOT}/Release")
set(CEF_RESOURCE_DIR "${CEF_ROOT}/Resources")
set(CEF_WRAPPER_DIR "${CEF_ROOT}/libcef_dll")
