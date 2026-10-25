# SPDX-License-Identifier: MPL-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(oskey_input_tests)
target_include_directories(app PRIVATE ../../display)
target_sources(app PRIVATE src/xpt2046.c src/keyboard.c ../../display/ui_keyboard.c)
