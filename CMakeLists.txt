# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app)

zephyr_get_compile_options_for_lang_as_string(C options)

# get_cmake_property(_variableNames VARIABLES)
# foreach (_variableName ${_variableNames})
#   message(STATUS "${_variableName}=${${_variableName}}")
# endforeach()


target_sources(app PRIVATE src/main.c
)

add_subdirectory(crypto)


include_directories(${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(app PRIVATE crypto)
