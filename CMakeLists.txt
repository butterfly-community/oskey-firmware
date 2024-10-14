# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hello_world)

add_subdirectory(crypto)

target_sources(app PRIVATE src/main.c)
target_link_libraries(app PRIVATE crypto)

target_include_directories(app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
