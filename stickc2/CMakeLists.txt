cmake_minimum_required(VERSION 3.16)

# Signal to main component that screen is available.
set(SURVAIV_HAS_DISPLAY TRUE CACHE BOOL "" FORCE)
set(ENV{SURVAIV_DISPLAY} "1")

# Auto-fetch LovyanGFX if not present.
set(LGFX_DIR "${CMAKE_CURRENT_LIST_DIR}/../boards/LovyanGFX")
if(NOT EXISTS "${LGFX_DIR}/CMakeLists.txt")
    message(STATUS "Fetching LovyanGFX (first build only)...")
    execute_process(
        COMMAND git clone --depth=1
            https://github.com/lovyan03/LovyanGFX.git
            "${LGFX_DIR}"
        RESULT_VARIABLE _git_result)
    if(NOT _git_result EQUAL 0)
        message(FATAL_ERROR "Failed to clone LovyanGFX")
    endif()
endif()

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../main"
    "${CMAKE_CURRENT_LIST_DIR}/../boards/screen"
    "${LGFX_DIR}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(survaiv-stickc2)
