cmake_minimum_required(VERSION 3.16)

set(SURVAIV_TIER "core" CACHE STRING "Build tier name" FORCE)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../main")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(survaiv-s3)
