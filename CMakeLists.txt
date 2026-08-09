cmake_minimum_required(VERSION 3.20.0)

# Project-local drivers ship their own devicetree bindings.
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/drivers)
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/lib/zscilib)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(oskey)

add_subdirectory(drivers)

if(CONFIG_OSKEY_FIDO2 AND CONFIG_NATIVE_LIBC)
  zephyr_compile_definitions(_POSIX_C_SOURCE=200809L)
endif()

include(cmake/lvgl.cmake)
include(cmake/audio.cmake)

target_sources(app PRIVATE
  src/main.c
  src/bus.c
  src/uart.c
  src/bluetooth/bluetooth.c
  src/storage.c
  src/net/wifi.c
  src/net/mqtt.c
  src/gpio.c
  src/usb/webusb.c
  src/app.c
  src/display/display.c
)

target_sources_ifdef(CONFIG_OSKEY_MCUBOOT app PRIVATE
  src/boot.c
)

target_sources_ifdef(CONFIG_OSKEY_RUST app PRIVATE
  src/core.c
)

target_sources_ifdef(CONFIG_OSKEY_TEST_FIRMWARE app PRIVATE
  src/test.c
)

target_sources_ifdef(CONFIG_OSKEY_CAMERA app PRIVATE
  src/camera/camera.c
)

if(CONFIG_OSKEY_QR_CODE)
  add_library(quirc STATIC
    lib/quirc/lib/decode.c
    lib/quirc/lib/identify.c
    lib/quirc/lib/quirc.c
    lib/quirc/lib/version_db.c
  )
  target_include_directories(quirc PUBLIC lib/quirc/lib)
  target_compile_definitions(quirc PRIVATE QUIRC_FLOAT_TYPE=float QUIRC_USE_TGMATH=1)
  target_link_libraries(quirc PUBLIC zephyr_interface)

  target_sources(app PRIVATE src/camera/qr_decoder.c)
  target_link_libraries(app PRIVATE quirc)
endif()

target_sources_ifdef(CONFIG_OSKEY_QR_SCANNER app PRIVATE
  src/camera/qr_scanner.c
  src/display/ui_qr.c
)

if(CONFIG_OSKEY_AUDIO)
  target_sources(app PRIVATE
    src/audio/audio.c
  )
  add_dependencies(app audio-assets)
endif()

target_sources_ifdef(CONFIG_OSKEY_IMU app PRIVATE src/imu/imu.c)

target_sources_ifdef(CONFIG_OSKEY_DISPLAY app PRIVATE
  src/display/ui.c
  src/display/ui_controller.c
  src/display/ui_status.c
  src/display/ui_dialog.c
  src/display/ui_pages.c
  src/display/ui_hw.c
  src/display/ui_confirmation.c
  src/display/assets/assets.c
)

target_sources_ifdef(CONFIG_OSKEY_WIFI app PRIVATE
  src/net/http.c
  src/net/wifi_portal.c
)

include(cmake/wifi.cmake)

target_sources_ifdef(CONFIG_OSKEY_USB app PRIVATE
  src/usb/bulk.c
  src/usb/init.c
)

target_sources_ifdef(CONFIG_OSKEY_FIDO2 app PRIVATE
  src/usb/fido2.c
)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

include(cmake/rust.cmake)
