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
      warning
      wifi
  )
    generate_inc_file_for_target(app
      ${CMAKE_CURRENT_SOURCE_DIR}/src/display/assets/${asset}.svg
      ${ZEPHYR_BINARY_DIR}/include/generated/oskey_${asset}.svg.inc
    )
  endforeach()
endif()

if(CONFIG_LV_USE_THORVG_INTERNAL)
  set(THORVG_DIR ${LVGL_DIR}/src/libs/thorvg)

  add_library(oskey_lvgl_thorvg STATIC)
  target_include_directories(oskey_lvgl_thorvg PRIVATE ${THORVG_DIR})
  target_compile_options(oskey_lvgl_thorvg PRIVATE
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/cmake/thorvg_config.h"
  )
  target_sources(oskey_lvgl_thorvg PRIVATE
    ${THORVG_DIR}/tvgCanvas.cpp
    ${THORVG_DIR}/tvgCapi.cpp
    ${THORVG_DIR}/tvgFill.cpp
    ${THORVG_DIR}/tvgInitializer.cpp
    ${THORVG_DIR}/tvgLoader.cpp
    ${THORVG_DIR}/tvgMath.cpp
    ${THORVG_DIR}/tvgPaint.cpp
    ${THORVG_DIR}/tvgPicture.cpp
    ${THORVG_DIR}/tvgRawLoader.cpp
    ${THORVG_DIR}/tvgRender.cpp
    ${THORVG_DIR}/tvgScene.cpp
    ${THORVG_DIR}/tvgShape.cpp
    ${THORVG_DIR}/tvgSwCanvas.cpp
    ${THORVG_DIR}/tvgSwFill.cpp
    ${THORVG_DIR}/tvgSwImage.cpp
    ${THORVG_DIR}/tvgSwMath.cpp
    ${THORVG_DIR}/tvgSwMemPool.cpp
    ${THORVG_DIR}/tvgSwPostEffect.cpp
    ${THORVG_DIR}/tvgSwRaster.cpp
    ${THORVG_DIR}/tvgSwRenderer.cpp
    ${THORVG_DIR}/tvgSwRle.cpp
    ${THORVG_DIR}/tvgSwShape.cpp
    ${THORVG_DIR}/tvgSwStroke.cpp
    ${THORVG_DIR}/tvgTaskScheduler.cpp
    ${THORVG_DIR}/tvgText.cpp
  )
  target_link_libraries(oskey_lvgl_thorvg PRIVATE zephyr_interface LVGL)
  target_link_libraries(app PRIVATE oskey_lvgl_thorvg)
endif()
