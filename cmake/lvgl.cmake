set(LVGL_DIR ${ZEPHYR_LVGL_MODULE_DIR})

if(CONFIG_LV_USE_DEMO_BENCHMARK)
  target_include_directories(app PRIVATE ${LVGL_DIR}/demos)
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
  ${LVGL_DIR}/demos/lv_demos.c
  ${LVGL_DIR}/demos/widgets/assets/img_clothes.c
  ${LVGL_DIR}/demos/widgets/assets/img_demo_widgets_avatar.c
  ${LVGL_DIR}/demos/widgets/assets/img_demo_widgets_needle.c
  ${LVGL_DIR}/demos/widgets/assets/img_lvgl_logo.c
  ${LVGL_DIR}/demos/widgets/lv_demo_widgets.c
  ${LVGL_DIR}/demos/widgets/lv_demo_widgets_analytics.c
  ${LVGL_DIR}/demos/widgets/lv_demo_widgets_components.c
  ${LVGL_DIR}/demos/widgets/lv_demo_widgets_profile.c
  ${LVGL_DIR}/demos/widgets/lv_demo_widgets_shop.c
)

if(CONFIG_OSKEY_DISPLAY)
  foreach(asset
      back
      bluetooth
      camera
      chevron_right
      document
      ethereum
      eye
      eye_off
      failure
      passkey
      refresh
      settings
      shuffle
      success
      trash
      usb
      wallet
      wallet_logo
      warning
      wifi
      wifi_ap
  )
    set(asset_file "${CMAKE_CURRENT_SOURCE_DIR}/src/display/assets/generated/${asset}.a8")
    if(NOT EXISTS "${asset_file}")
      message(FATAL_ERROR "Missing generated display asset: ${asset_file}")
    endif()
    generate_inc_file_for_target(app
      ${asset_file}
      ${ZEPHYR_BINARY_DIR}/include/generated/oskey_${asset}.a8.inc
    )
  endforeach()
endif()

add_custom_target(display-assets
  COMMAND ${CMAKE_COMMAND}
    "-DASSET_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/src/display/assets"
    "-DASSET_OUTPUT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/src/display/assets/generated"
    "-DASSET_TEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/display-assets"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/display_assets.cmake"
  USES_TERMINAL
  VERBATIM
)
