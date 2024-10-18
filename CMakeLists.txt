cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app)

zephyr_get_compile_options_for_lang_as_string(C options)

# get_cmake_property(_variableNames VARIABLES) foreach (_variableName
# ${_variableNames}) message(STATUS "${_variableName}=${${_variableName}}")
# endforeach()

add_subdirectory(crypto)

file(
  GENERATE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/compile_options.txt"
  CONTENT "${options}")


string(
  REGEX
  REPLACE
    "\\$<SEMICOLON>\\$<TARGET_PROPERTY:compiler,imacros>\\$<SEMICOLON>[^$]+/autoconf\\.h"
    ""
    options
    "${options}")

separate_arguments(options_list UNIX_COMMAND "${options}")

target_compile_options(crypto PRIVATE ${options_list})

target_sources(app PRIVATE src/main.c)

target_link_libraries(app PRIVATE crypto)
