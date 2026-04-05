cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SURVAIV_TIER)
    set(SURVAIV_TIER "pico" CACHE STRING "Build tier name" FORCE)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(survaiv)
