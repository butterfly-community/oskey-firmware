cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ohw)

set(PROJECT_ROOT_DIR ${CMAKE_SOURCE_DIR} CACHE INTERNAL "Project root directory")

set(LVGL_DIR ${ZEPHYR_LVGL_MODULE_DIR})

if(CONFIG_LV_USE_DEMO_BENCHMARK)
    target_include_directories(app PRIVATE
        ${LVGL_DIR}/demos/benchmark
    )
endif()

target_sources_ifdef(CONFIG_LV_USE_DEMO_BENCHMARK app PRIVATE
   ${LVGL_DIR}/demos/benchmark/assets/img_benchmark_avatar.c
    ${LVGL_DIR}/demos/benchmark/assets/img_benchmark_lvgl_logo_argb.c
    ${LVGL_DIR}/demos/benchmark/assets/img_benchmark_lvgl_logo_rgb.c
    ${LVGL_DIR}/demos/benchmark/assets/lv_font_benchmark_montserrat_12_aligned.c
    ${LVGL_DIR}/demos/benchmark/assets/lv_font_benchmark_montserrat_14_aligned.c
    ${LVGL_DIR}/demos/benchmark/assets/lv_font_benchmark_montserrat_16_aligned.c
    ${LVGL_DIR}/demos/benchmark/assets/lv_font_benchmark_montserrat_18_aligned.c
    ${LVGL_DIR}/demos/benchmark/assets/lv_font_benchmark_montserrat_20_aligned.c
    ${LVGL_DIR}/demos/benchmark/assets/lv_font_benchmark_montserrat_24_aligned.c
    ${LVGL_DIR}/demos/benchmark/assets/lv_font_benchmark_montserrat_26_aligned.c
    ${LVGL_DIR}/demos/benchmark/lv_demo_benchmark.c
)


target_sources_ifdef(CONFIG_LV_USE_DEMO_WIDGETS app PRIVATE
    ${LVGL_DIR}/demos/widgets/assets/img_clothes.c
    ${LVGL_DIR}/demos/widgets/assets/img_demo_widgets_avatar.c
    ${LVGL_DIR}/demos/widgets/assets/img_demo_widgets_needle.c
    ${LVGL_DIR}/demos/widgets/assets/img_lvgl_logo.c
    ${LVGL_DIR}/demos/widgets/lv_demo_widgets.c
)


# FILE(GLOB app_sources src/*.c)
# target_sources(app PRIVATE ${app_sources})

zephyr_get_compile_options_for_lang_as_string(C options)

# get_cmake_property(_variableNames VARIABLES) foreach (_variableName
# ${_variableNames}) message(STATUS "${_variableName}=${${_variableName}}")
# endforeach()

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

add_subdirectory(temp)
add_subdirectory(wallets/psa)

target_sources(app PRIVATE
  src/main.c
  src/uart.c
  src/bluetooth.c
  src/storage.c
  src/boot.c
  src/wifi.c
  src/display/screen.c)

target_compile_options(temp PRIVATE ${options_list})

target_compile_options(crypto PRIVATE ${options_list})

target_link_libraries(crypto PRIVATE zephyr_interface mbedTLS)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/wallets)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/temp)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

rust_cargo_application()

target_link_libraries(app PRIVATE crypto temp)
